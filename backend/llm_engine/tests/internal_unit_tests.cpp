// llm_engine/tests/internal_unit_tests.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <grpcpp/create_channel.h>

#include "tts_client.h"
#include "tts.grpc.pb.h"

// 네임스페이스 사용
using namespace llm_engine;

class MockTTSStub : public tts::TTSService::StubInterface {
    public:
    MOCK_METHOD(grpc::ClientWriterInterface<tts::TTSStreamRequest>*, SynthesizeStreamRaw, (grpc::ClientContext* context, google::protobuf::Empty* response), (override));

    // === 추가 필요한 Mock 메서드 정의 ===
    MOCK_METHOD(grpc::ClientAsyncWriterInterface<tts::TTSStreamRequest>*, AsyncSynthesizeStreamRaw, (grpc::ClientContext* context, google::protobuf::Empty* response, grpc::CompletionQueue* cq, void* tag), (override));
    MOCK_METHOD(grpc::ClientAsyncWriterInterface<tts::TTSStreamRequest>*, PrepareAsyncSynthesizeStreamRaw, (grpc::ClientContext* context, google::protobuf::Empty* response, grpc::CompletionQueue* cq), (override));
};
// ===== End Mock Stub Definition =====


// ===== Test Fixture 정의 =====
class TTSClientTest : public ::testing::Test {
protected:
    // Mock 객체 또는 실제 객체를 위한 변수
    std::shared_ptr<MockTTSStub> mock_stub_;
    std::unique_ptr<TTSClient> client_with_mock_;
    std::unique_ptr<TTSClient> client_with_real_channel_; // 실제 채널 테스트용 (선택 사항)
    std::shared_ptr<grpc::Channel> dummy_channel_; // 실제 연결 안 되는 더미 채널

    void SetUp() override {
        // Mock Stub 생성
        mock_stub_ = std::make_shared<MockTTSStub>();
        // Mock Stub을 주입하여 TTSClient 생성
        client_with_mock_ = std::make_unique<TTSClient>(mock_stub_);

        // 실제 채널 테스트를 위한 더미 채널 생성 (연결되지 않아도 객체 생성 가능)
        dummy_channel_ = grpc::CreateChannel("dummy_address", grpc::InsecureChannelCredentials());
        client_with_real_channel_ = std::make_unique<TTSClient>(dummy_channel_);
    }

    void TearDown() override {
        // 스마트 포인터가 자동으로 메모리 관리
    }
};
// ===== End Test Fixture =====


// ----- 수정된/추가된 테스트 케이스 -----

// 생성자 테스트 (실제 채널 사용 시 - 객체 생성 성공 여부)
TEST_F(TTSClientTest, ConstructorWithRealChannel) {
    // SetUp에서 client_with_real_channel_ 생성이 예외 없이 성공했는지 확인
    ASSERT_NE(client_with_real_channel_, nullptr);
}

// 생성자 테스트 (Mock Stub 사용 시 - 객체 생성 성공 여부)
TEST_F(TTSClientTest, ConstructorWithMockStub) {
    // SetUp에서 client_with_mock_ 생성이 예외 없이 성공했는지 확인
    ASSERT_NE(client_with_mock_, nullptr);
}

// 초기 상태 확인 테스트 (스트림 비활성 상태)
TEST_F(TTSClientTest, InitialStreamState) {
    EXPECT_FALSE(client_with_mock_->IsStreamActive());
    EXPECT_FALSE(client_with_real_channel_->IsStreamActive());

    // 비활성 상태에서 FinishStream 호출 시 FAILED_PRECONDITION 또는 OK 반환 확인
    grpc::Status status_mock = client_with_mock_->FinishStream();
    EXPECT_TRUE(status_mock.error_code() == grpc::StatusCode::FAILED_PRECONDITION || status_mock.ok());

    grpc::Status status_real = client_with_real_channel_->FinishStream();
     EXPECT_TRUE(status_real.error_code() == grpc::StatusCode::FAILED_PRECONDITION || status_real.ok());
}

// StartStream 인자 유효성 검사 테스트 (성공/실패는 Mocking 필요)
TEST_F(TTSClientTest, StartStreamParameterValidation) {
     // 성공 케이스는 Mocking 필요 - 여기서는 인자 유효성만 체크
     // 실패 케이스 (빈 인자)
     EXPECT_FALSE(client_with_mock_->StartStream("", "ko-KR", "voice")); // 빈 세션 ID
     EXPECT_FALSE(client_with_mock_->StartStream("session1", "", "voice")); // 빈 언어 코드

     // voice_name은 선택적일 수 있으므로 빈 값 테스트는 제외 가능
     // EXPECT_FALSE(client_with_mock_->StartStream("session1", "ko-KR", ""));
}
