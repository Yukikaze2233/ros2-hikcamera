#include "test_support.hpp"
#include <set>
#include <cerrno>

static void test_two_readers() {
    // Broadcast latest-frame semantics: each write is immediately consumed
    // by both readers independently. No assumption of replay — each pair
    // sees the same latest frame with correct pixels and metadata.
    auto name = make_name("tr"); shm_unlink(name.c_str());
    auto wr = SharedFrameWriter::create(name.c_str()); REQUIRE(wr.has_value());
    auto writer = std::move(wr.value());
    SharedFrameReader r1,r2;
    REQUIRE(r1.open(name.c_str()).has_value());
    REQUIRE(r2.open(name.c_str()).has_value());
    constexpr uint32_t kW=640,kH=480; constexpr size_t kB=kW*kH*3;
    std::vector<unsigned char> buf(kB);

    for (uint64_t s = 1; s <= 3; ++s) {
        fill_pattern(buf, s);
        FrameMetadata m{};
        m.width=kW; m.height=kH; m.frame_id=s*100;
        m.device_timestamp_ticks=s*1000;
        m.exposure_us=static_cast<uint32_t>(15000+s*100);
        REQUIRE(writer.write(m, buf).has_value());

        // Both readers independently observe the same latest committed frame.
        auto f1 = r1.wait_next(5s); REQUIRE(f1.has_value());
        CHECK_EQ(f1->sequence(), s);
        CHECK_EQ(f1->metadata().frame_id, s*100U);
        CHECK_EQ(f1->metadata().pixel_format, PixelFormat::BGR8);
        verify_pattern(std::span<const unsigned char>(
            static_cast<const unsigned char*>(f1->mat().data), kB), s);

        auto f2 = r2.wait_next(5s); REQUIRE(f2.has_value());
        CHECK_EQ(f2->sequence(), s);
        CHECK_EQ(f2->metadata().frame_id, s*100U);
        CHECK_EQ(f2->metadata().pixel_format, PixelFormat::BGR8);
        verify_pattern(std::span<const unsigned char>(
            static_cast<const unsigned char*>(f2->mat().data), kB), s);
    }
}

static void test_lease_tear() {
    auto name = make_name("lt"); shm_unlink(name.c_str());
    auto wr = SharedFrameWriter::create(name.c_str()); REQUIRE(wr.has_value());
    auto writer = std::move(wr.value());
    constexpr uint32_t kW=320,kH=240; constexpr size_t kB=kW*kH*3;
    std::vector<unsigned char> buf(kB);

    // Write frame 1 → slot 0.
    fill_pattern(buf,1);
    FrameMetadata m1{};m1.width=kW;m1.height=kH;
    REQUIRE(writer.write(m1,buf).has_value());

    // Reader acquires lease on slot 0.
    SharedFrameReader reader;
    REQUIRE(reader.open(name.c_str()).has_value());
    auto f=reader.wait_next(5s);REQUIRE(f.has_value());
    CHECK_EQ(f->sequence(),1U);

    // Attach a second mapping to probe the slot lock deterministically.
    int fd=shm_open(name.c_str(),O_RDWR,0);REQUIRE(fd!=-1);
    auto* ring=reinterpret_cast<SharedRingHeader*>(
        mmap(nullptr,sizeof(SharedRingHeader),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
    close(fd);REQUIRE(ring!=MAP_FAILED);
    auto cleanup=[&]{munmap(ring,sizeof(SharedRingHeader));};

    // Prove slot 0 is read-locked: trywrlock must return EBUSY.
    int rc=pthread_rwlock_trywrlock(&ring->slots[0].lock);
    CHECK_EQ(rc,EBUSY);

    // Release the lease — trywrlock must now succeed.
    f=SharedFrame{};
    rc=pthread_rwlock_trywrlock(&ring->slots[0].lock);
    CHECK_EQ(rc,0);
    pthread_rwlock_unlock(&ring->slots[0].lock);

    // Write wraparound frames (2,3,4 → slots 1,2,3; 5 → slot 0).
    for(uint64_t s=2;s<=5;++s){
        fill_pattern(buf,s);
        FrameMetadata mm{};mm.width=kW;mm.height=kH;
        REQUIRE(writer.write(mm,buf).has_value());
    }

    // Reader gets latest frame with correct pixels.
    auto f5=reader.wait_next(5s);REQUIRE(f5.has_value());
    CHECK_EQ(f5->sequence(),5U);
    verify_pattern(std::span<const unsigned char>(
        static_cast<const unsigned char*>(f5->mat().data),kB),5);
    cleanup();
}

static void test_lagged_reader() {
    auto name=make_name("lg");shm_unlink(name.c_str());
    auto wr=SharedFrameWriter::create(name.c_str());REQUIRE(wr.has_value());
    auto writer=std::move(wr.value());
    SharedFrameReader reader;REQUIRE(reader.open(name.c_str()).has_value());
    constexpr size_t kB=320*240*3;std::vector<unsigned char> buf(kB);
    for(uint64_t s=1;s<=10;++s){fill_pattern(buf,s);FrameMetadata m{};m.width=320;m.height=240;REQUIRE(writer.write(m,buf).has_value());}
    auto f=reader.wait_next(5s);REQUIRE(f.has_value());CHECK_EQ(f->sequence(),10U);
    fill_pattern(buf,11);FrameMetadata m{};m.width=320;m.height=240;REQUIRE(writer.write(m,buf).has_value());
    auto f2=reader.wait_next(5s);REQUIRE(f2.has_value());CHECK_EQ(f2->sequence(),11U);
}

static void test_metadata_roundtrip() {
    auto name=make_name("mr");shm_unlink(name.c_str());
    auto wr=SharedFrameWriter::create(name.c_str());REQUIRE(wr.has_value());
    auto writer=std::move(wr.value());
    SharedFrameReader reader;REQUIRE(reader.open(name.c_str()).has_value());
    constexpr size_t kB=1920*1080*3;std::vector<unsigned char> buf(kB);fill_pattern(buf,42);
    FrameMetadata m{};m.frame_id=12345;m.device_timestamp_ticks=0xDEADBEEFCAFE1234ULL;
    m.host_monotonic_ns=9876543210123456789ULL;m.exposure_us=15500;m.width=1920;m.height=1080;m.stride_bytes=1920*3;
    REQUIRE(writer.write(m,buf).has_value());
    auto f=reader.wait_next(5s);REQUIRE(f.has_value());const auto& fm=f->metadata();
    CHECK_GT(fm.committed_sequence,0U);CHECK_EQ(fm.frame_id,12345U);
    CHECK_EQ(fm.device_timestamp_ticks,0xDEADBEEFCAFE1234ULL);
    CHECK_EQ(fm.host_monotonic_ns,9876543210123456789ULL);CHECK_EQ(fm.exposure_us,15500U);
    CHECK_EQ(fm.width,1920U);CHECK_EQ(fm.stride_bytes,1920U*3U);
    CHECK_GT(fm.committed_bytes,0U);CHECK_EQ(fm.pixel_format,PixelFormat::BGR8);
}

static void test_creator_exclusive() {
    auto name=make_name("ce");shm_unlink(name.c_str());
    auto w1=SharedFrameWriter::create(name.c_str());REQUIRE(w1.has_value());
    auto w2=SharedFrameWriter::create(name.c_str());CHECK(!w2.has_value());
    SharedFrameReader reader;REQUIRE(reader.open(name.c_str()).has_value());
}

static void test_lease_lifetime() {
    auto name=make_name("ll");shm_unlink(name.c_str());
    auto wr=SharedFrameWriter::create(name.c_str());REQUIRE(wr.has_value());
    auto writer=std::move(wr.value());
    constexpr size_t kB=640*480*3;std::vector<unsigned char> buf(kB);fill_pattern(buf,1);
    FrameMetadata m{};m.width=640;m.height=480;REQUIRE(writer.write(m,buf).has_value());
    SharedFrame frame;
    {SharedFrameReader r;REQUIRE(r.open(name.c_str()).has_value());auto fe=r.wait_next(5s);REQUIRE(fe.has_value());CHECK_EQ(fe->sequence(),1U);frame=*fe;}
    CHECK(frame.valid());CHECK_EQ(frame.sequence(),1U);
    verify_pattern(std::span<const unsigned char>(static_cast<const unsigned char*>(frame.mat().data),kB),1);
    frame=SharedFrame{};SharedFrameReader r2;REQUIRE(r2.open(name.c_str()).has_value());
}

static void test_concurrent_writes() {
    auto name=make_name("cw");shm_unlink(name.c_str());
    auto wr=SharedFrameWriter::create(name.c_str());REQUIRE(wr.has_value());
    auto writer=std::move(wr.value());
    constexpr int kT=4,kW=20,kTotal=kT*kW;constexpr size_t kB=320*240*3;
    std::atomic<int> ready{0}, wdone{0}, wfail{0};
    std::vector<std::thread> th;
    for(int t=0;t<kT;++t)
        th.emplace_back([&,t]{
            std::vector<unsigned char> b(kB);
            ready.fetch_add(1);while(ready.load()<kT)std::this_thread::yield();
            for(int i=0;i<kW;++i){
                fill_pattern(b,static_cast<uint64_t>(t)*1000+i);
                FrameMetadata m{};m.width=320;m.height=240;
                auto r=writer.write(m,b);TCHECK(r.has_value(),wfail);
            }
            wdone.fetch_add(1);
        });
    SharedFrameReader reader;REQUIRE(reader.open(name.c_str()).has_value());
    uint64_t last=0;int cnt=0;
    for(;;){
        auto f=reader.wait_next(500ms);
        if(!f.has_value()){
            // Timeout is error unless all writers finished AND we saw the last sequence.
            if(wdone.load()==kT&&last>=static_cast<uint64_t>(kTotal))break;
            CHECK(!"Timeout before all writers finished");
            break;
        }
        CHECK_GT(f->sequence(),last);last=f->sequence();++cnt;
        if(last>=static_cast<uint64_t>(kTotal))break;
    }
    for(auto&t:th)t.join();
    CHECK(!wfail.load());CHECK_EQ(last,static_cast<uint64_t>(kTotal));CHECK_GT(cnt,0);
}

static void test_boundary_reject() {
    auto name=make_name("br");shm_unlink(name.c_str());
    auto wr=SharedFrameWriter::create(name.c_str());REQUIRE(wr.has_value());
    auto writer=std::move(wr.value());
    std::vector<unsigned char> b(100);fill_pattern(b,0);
    {FrameMetadata m{};m.width=0;m.height=100;CHECK(!writer.write(m,b).has_value());}
    {FrameMetadata m{};m.width=100;m.height=0;CHECK(!writer.write(m,b).has_value());}
    {FrameMetadata m{};m.width=100;m.height=100;m.stride_bytes=100*3-1;CHECK(!writer.write(m,b).has_value());}
    {FrameMetadata m{};m.width=100;m.height=100;CHECK(!writer.write(m,b).has_value());}
    {std::vector<unsigned char> big(kShmMaxPixelBytes+1);FrameMetadata m{};m.width=5472;m.height=3648;CHECK(!writer.write(m,big).has_value());}
}

int main(int argc,char**argv){
    if(argc<2){std::cerr<<"Usage: test_shm <name>\n";return 1;}
    std::string t=argv[1];
    if(t=="two_readers")test_two_readers();
    else if(t=="lease_tear")test_lease_tear();
    else if(t=="lagged_reader")test_lagged_reader();
    else if(t=="metadata_roundtrip")test_metadata_roundtrip();
    else if(t=="creator_exclusive")test_creator_exclusive();
    else if(t=="lease_lifetime")test_lease_lifetime();
    else if(t=="concurrent_writes")test_concurrent_writes();
    else if(t=="boundary_reject")test_boundary_reject();
    else{std::cerr<<"Unknown: "<<t<<'\n';return 1;}
    int f=g_failures.load();
    if(f==0){std::cout<<"PASS "<<t<<'\n';return 0;}
    std::cerr<<f<<" FAILURE(S) in "<<t<<'\n';return 1;
}
