#include <catch2/catch_all.hpp>

#include "irods/client_connection.hpp"
#include "irods/connection_pool.hpp"
#include "irods/dstream.hpp"
#include "irods/filesystem.hpp"
#include "irods/get_file_descriptor_info.h"
#include "irods/irods_at_scope_exit.hpp"
#include "irods/rodsClient.h"
#include "irods/transport/default_transport.hpp"

#include <nlohmann/json.hpp>

TEST_CASE("get_file_descriptor_info")
{
    // clang-format off
    namespace fs = irods::experimental::filesystem;
    namespace io = irods::experimental::io;
    using json   = nlohmann::json;
    // clang-format on

    load_client_api_plugins();

    rodsEnv env;
    _getRodsEnv(env);

    auto conn_pool = irods::make_connection_pool();
    auto conn = conn_pool->get_connection();
    const auto sandbox = fs::path{env.rodsHome} / "unit_testing_sandbox";

    if (!fs::client::exists(conn, sandbox)) {
        REQUIRE(fs::client::create_collection(conn, sandbox));
    }

    irods::at_scope_exit remove_sandbox{[&conn, &sandbox] {
        REQUIRE(fs::client::remove_all(conn, sandbox, fs::remove_options::no_trash));
    }};

    const auto path = sandbox / "data_object.txt";
    char* json_output = nullptr;

    irods::at_scope_exit free_memory{[&json_output] {
        if (json_output) {
            std::free(json_output);
        }
    }};

    // Guarantees that the stream is closed before clean up.
    {
        io::client::default_transport tp{conn};
        io::odstream out{tp, path};
        REQUIRE(out.is_open());

        const auto json_input = json{{"fd", out.file_descriptor()}}.dump();

        REQUIRE(rc_get_file_descriptor_info(static_cast<rcComm_t*>(conn), json_input.c_str(), &json_output) == 0);
    }

    json info;
    REQUIRE_NOTHROW(info = json::parse(json_output));

    // Verify existence of properties.
    REQUIRE(info.count("l3descInx"));
    REQUIRE(info.count("data_object_input_replica_flag"));
    REQUIRE(info.count("data_object_input"));
    REQUIRE(info.count("data_object_info"));
    REQUIRE(info.count("other_data_object_info"));
    REQUIRE(info.count("data_size"));
}

TEST_CASE("#7338")
{
    load_client_api_plugins();

    irods::experimental::client_connection conn{irods::experimental::defer_authentication};

    char* json_error_string{};

    CHECK(rc_get_file_descriptor_info(static_cast<RcComm*>(conn), "", &json_error_string) == SYS_NO_API_PRIV);
    CHECK(json_error_string == nullptr);
}
