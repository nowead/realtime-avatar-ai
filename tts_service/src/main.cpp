#include <iostream>
#include <grpcpp/grpcpp.h>
#include "tts.grpc.pb.h"
#include "tts_service.h"

using grpc::Server;
using grpc::ServerBuilder;

int main() {
    std::string server_address("0.0.0.0:50052");
    tts::TTSServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "TTS Server listening on " << server_address << std::endl;
    server->Wait();

    return 0;
}
