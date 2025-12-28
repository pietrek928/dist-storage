#include <utils/task_runner.h>
#include <grpc/callback.h>

#include <message.pb.h>
#include <message.grpc.pb.h>


class MessageSendHandler : public GRPCBasicHandler<
    message::Message::AsyncService, message::SignedMessage, message::SendMessageResponse
> {
    // TODO: error logging
    // TODO: auth and validate message
    void handle_request() {
    }
};
