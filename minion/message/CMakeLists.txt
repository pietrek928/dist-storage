add_executable(message_server message.cc message_send.cc tell_my_addr.cc message_stream.cc stream_router.cc recipient_dispatch.cc)

target_link_libraries(message_server PRIVATE
    dist_storage_common
    my_proto_lib
    dist_storage_grpc
    dist_storage_peer
)
