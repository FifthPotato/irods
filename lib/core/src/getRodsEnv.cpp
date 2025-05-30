/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/*
  This routine sets up the rodsEnv structure using the contents of the
  irods_environment.json file and possibly some environment variables.
  For each of the irods_environment.json  items, if an environment variable
  with the same name exists, it overrides the possible environment item.
  This is called by the various Rods commands and the agent.

  This routine also fills in irodsHome and irodsCwd if they are not
  otherwise defined, and if values needed to create them are available.

  The '#' character indicates a comment line.  Items may be enclosed in
  quotes, but do not need to be.  One or more spaces, or a '=', will
  precede the item values.

  The items are defined in the rodsEnv struct.

  If an error occurs, a message may logged or displayed but the
  structure is filled with whatever values are available.

  There is also an 'appendRodsEnv' function to add text to
  the env file, either creating it or appending to it.
*/

#include "irods/rodsDef.h"
#include "irods/rods.h"
#include "irods/rodsErrorTable.h"
#include "irods/getRodsEnv.h"
#include "irods/rodsLog.h"
#include "irods/irods_log.hpp"
#include "irods/irods_version.h"
#include "irods/irods_environment_properties.hpp"
#include "irods/irods_configuration_keywords.hpp"
#include "irods/irods_server_properties.hpp"

#include <nlohmann/json.hpp>

#include <cstring>

#define BUF_LEN 100
#define LARGE_BUF_LEN MAX_NAME_LEN+20

#define RODS_ENV_FILE "/.irods/irods_environment.json"  /* under the HOME directory */

namespace
{
    void init_from_server_properties(rodsEnv& _env)
    {
        // The following members are not used by the server:
        //
        //    - rodsAuthFile

        // iRODS 5 servers always request client-server negotiation on redirects.
        std::strncpy(
            _env.rodsClientServerNegotiation, REQ_SVR_NEG, sizeof(RodsEnvironment::rodsClientServerNegotiation) - 1);

        const auto config_handle = irods::server_properties::instance().map();
        const auto& config = config_handle.get_json();

        using json = nlohmann::json;

        // clang-format off
        const auto copy_string = []<std::size_t N>(const json& _config, const char* const _k, char (&_v)[N]) {
            _config.at(_k).get_ref<const std::string&>().copy(_v, N - 1);
        };

        const auto copy_int = [](const json& _config, const char* const _k, int& _v) {
            _v = _config.at(_k).get<int>();
        };
        // clang-format on

        copy_string(config, irods::KW_CFG_HOST, _env.rodsHost);
        copy_int(config, irods::KW_CFG_ZONE_PORT, _env.rodsPort);

        copy_string(config, irods::KW_CFG_ZONE_NAME, _env.rodsZone);
        copy_string(config, irods::KW_CFG_ZONE_USER, _env.rodsUserName);
        copy_string(config, irods::KW_CFG_ZONE_AUTH_SCHEME, _env.rodsAuthScheme);

        copy_string(config, irods::KW_CFG_CLIENT_SERVER_POLICY, _env.rodsClientServerPolicy);

        const auto& encryption = config.at(irods::KW_CFG_ENCRYPTION);
        copy_string(encryption, irods::KW_CFG_ENCRYPTION_ALGORITHM, _env.rodsEncryptionAlgorithm);
        copy_int(encryption, irods::KW_CFG_ENCRYPTION_KEY_SIZE, _env.rodsEncryptionKeySize);
        copy_int(encryption, irods::KW_CFG_ENCRYPTION_NUM_HASH_ROUNDS, _env.rodsEncryptionNumHashRounds);
        copy_int(encryption, irods::KW_CFG_ENCRYPTION_SALT_SIZE, _env.rodsEncryptionSaltSize);

        copy_string(config, irods::KW_CFG_DEFAULT_HASH_SCHEME, _env.rodsDefaultHashScheme);
        copy_string(config, irods::KW_CFG_MATCH_HASH_POLICY, _env.rodsMatchHashPolicy);

        copy_string(config, irods::KW_CFG_DEFAULT_RESOURCE_NAME, _env.rodsDefResource);
        copy_int(config, irods::KW_CFG_CONNECTION_POOL_REFRESH_TIME, _env.irodsConnectionPoolRefreshTime);

        if (const auto tls_iter = config.find(irods::KW_CFG_TLS_CLIENT); tls_iter != std::end(config)) {
            // clang-format off
            const auto copy_tls_string = []<std::size_t N>(char (&_dst)[N], const nlohmann::json& _src) {
                _src.get_ref<const std::string&>().copy(_dst, N - 1);
            };
            // clang-format on

            for (auto&& [k, v] : tls_iter->items()) {
                if (irods::KW_CFG_TLS_CA_CERTIFICATE_FILE == k) {
                    copy_tls_string(_env.irodsSSLCACertificateFile, v);
                }
                else if (irods::KW_CFG_TLS_CA_CERTIFICATE_PATH == k) {
                    copy_tls_string(_env.irodsSSLCACertificatePath, v);
                }
                else if (irods::KW_CFG_TLS_VERIFY_SERVER == k) {
                    copy_tls_string(_env.irodsSSLVerifyServer, v);
                }
            }
        }

        // If the configuration is not set for the TCP keepalive options, set the value to something invalid. This
        // indicates that we should not set the option on the socket, which will allow the socket to use the kernel
        // configuration.
        _env.tcp_keepalive_intvl = config.value(irods::KW_CFG_TCP_KEEPALIVE_INTVL_IN_SECONDS, -1);
        if (_env.tcp_keepalive_intvl < 0) {
            _env.tcp_keepalive_intvl = -1;
        }

        _env.tcp_keepalive_probes = config.value(irods::KW_CFG_TCP_KEEPALIVE_PROBES, -1);
        if (_env.tcp_keepalive_probes < 0) {
            _env.tcp_keepalive_probes = -1;
        }

        _env.tcp_keepalive_time = config.value(irods::KW_CFG_TCP_KEEPALIVE_TIME_IN_SECONDS, -1);
        if (_env.tcp_keepalive_time < 0) {
            _env.tcp_keepalive_time = -1;
        }

        const auto& advanced_settings = config.at(irods::KW_CFG_ADVANCED_SETTINGS);
        copy_int(advanced_settings, irods::KW_CFG_DEF_NUMBER_TRANSFER_THREADS, _env.irodsDefaultNumberTransferThreads);
        copy_int(advanced_settings, irods::KW_CFG_MAX_SIZE_FOR_SINGLE_BUFFER, _env.irodsMaxSizeForSingleBuffer);
        copy_int(
            advanced_settings, irods::KW_CFG_TRANS_BUFFER_SIZE_FOR_PARA_TRANS, _env.irodsTransBufferSizeForParaTrans);

        if (auto iter = config.find(irods::KW_CFG_PLUGIN_DIRECTORY); iter != std::end(config)) {
            iter->get_ref<const std::string&>().copy(
                _env.irodsPluginDirectory, sizeof(rodsEnv::irodsPluginDirectory) - 1);
        }
    } // init_from_server_properties
} // anonymous namespace

extern "C" {

    extern int ProcessType;
    extern char *rstrcpy( char *dst, const char *src, int len ); // why do they not just include the header? - harry

    char *findNextTokenAndTerm( char *inPtr );

    int getRodsEnvFromFile( rodsEnv *rodsEnvArg );
    int getRodsEnvFromEnv( rodsEnv *rodsEnvArg );
    int createRodsEnvDefaults( rodsEnv *rodsEnvArg );

    static char authFileName  [ LONG_NAME_LEN ] = "";
    static char configFileName[ LONG_NAME_LEN ] = "";

    char *
    getRodsEnvFileName() {
        return configFileName;
    }

    /* Return the auth filename, if any */
    /* Used by obf routines so that the env struct doesn't have to be passed
       up and down the calling chain */
    char *
    getRodsEnvAuthFileName() {
        return authFileName;
    }

    /* convert either an integer value or a name matching the defines, to
       a value for the Logging Level */
    int
    convertLogLevel( char *inputStr ) {
        int i;
        i = atoi( inputStr );
        if ( i > 0 && i <= LOG_SQL ) {
            return i;
        }
        if ( strcmp( inputStr, "LOG_SQL" ) == 0 ) {
            return LOG_SQL;
        }
        if ( strcmp( inputStr, "LOG_SYS_FATAL" ) == 0 ) {
            return LOG_SYS_FATAL;
        }
        if ( strcmp( inputStr, "LOG_SYS_WARNING" ) == 0 ) {
            return LOG_SYS_WARNING;
        }
        if ( strcmp( inputStr, "LOG_ERROR" ) == 0 ) {
            return LOG_ERROR;
        }
        if ( strcmp( inputStr, "LOG_NOTICE" ) == 0 ) {
            return LOG_NOTICE;
        }
        if ( strcmp( inputStr, "LOG_DEBUG" ) == 0 ) {
            return LOG_DEBUG;
        }
        if ( strcmp( inputStr, "LOG_DEBUG6" ) == 0 ) {
            return LOG_DEBUG6;
        }
        if ( strcmp( inputStr, "LOG_DEBUG7" ) == 0 ) {
            return LOG_DEBUG7;
        }
        if ( strcmp( inputStr, "LOG_DEBUG8" ) == 0 ) {
            return LOG_DEBUG8;
        }
        if ( strcmp( inputStr, "LOG_DEBUG9" ) == 0 ) {
            return LOG_DEBUG9;
        }
        if ( strcmp( inputStr, "LOG_DEBUG10" ) == 0 ) {
            return LOG_DEBUG10;
        }
        return 0;
    }

    int getRodsEnv( rodsEnv *rodsEnvArg ) {
        if ( !rodsEnvArg ) {
            return SYS_INVALID_INPUT_PARAM;
        }
        _getRodsEnv( *rodsEnvArg );

        return 0;
    }

    void _getRodsEnv(rodsEnv& rodsEnvArg)
    {
        std::memset(&rodsEnvArg, 0, sizeof(rodsEnv));

        if (CLIENT_PT != ::ProcessType) {
            init_from_server_properties(rodsEnvArg);
            return;
        }

        getRodsEnvFromFile( &rodsEnvArg );
        getRodsEnvFromEnv( &rodsEnvArg );
        createRodsEnvDefaults( &rodsEnvArg );
    }

    void _reloadRodsEnv(rodsEnv& rodsEnvArg)
    {
        memset(&rodsEnvArg, 0, sizeof(rodsEnv));

        if (CLIENT_PT != ::ProcessType) {
            init_from_server_properties(rodsEnvArg);
            return;
        }

        try {
            irods::environment_properties::instance().capture();
        } catch ( const irods::exception& e ) {
            irods::log(e);
            return;
        }

        getRodsEnvFromFile( &rodsEnvArg );
        getRodsEnvFromEnv( &rodsEnvArg );
        createRodsEnvDefaults( &rodsEnvArg );
    }

    int capture_string_property(const char* _key, char* _val, size_t _val_size)
    {
        if (_key == nullptr || _val == nullptr || _val_size == 0) {
            return SYS_GETENV_ERR;
        }
        std::string key{_key};

        try {
            auto property = irods::get_environment_property<const std::string>(key);

            // If truncation would occur because property.size() >= _val_size, return an error
            if (property.size() >= _val_size) {
                return SYS_GETENV_ERR;
            }
            std::strncpy(_val, property.c_str(), _val_size);
            return 0;
        } catch ( const irods::exception& e ) {
            if ( e.code() == KEY_NOT_FOUND ) {
                rodsLog(LOG_DEBUG10, "%s is not defined", _key);
            } else {
                irods::log(e);
            }
            return e.code();
        }

    } // capture_string_property

    static
    int capture_integer_property(
        const std::string&             _key,
        int&                           _val ) {
        try {
            _val = irods::get_environment_property<const int>(_key);
            return 0;
        } catch ( const irods::exception& e ) {
            if ( e.code() == KEY_NOT_FOUND ) {
                rodsLog( LOG_DEBUG10, "%s is not defined", _key.c_str() );
            } else {
                irods::log(e);
            }
            return e.code();
        }

    } // capture_integer_property

    int getRodsEnvFromFile(
        rodsEnv* _env ) {
        if ( !_env ) {
            return SYS_INVALID_INPUT_PARAM;
        }

        // defaults for advanced settings
        _env->irodsMaxSizeForSingleBuffer       = 32;
        _env->irodsDefaultNumberTransferThreads = 4;
        _env->irodsTransBufferSizeForParaTrans  = 4;
        _env->irodsConnectionPoolRefreshTime    = 300;

        // default auth scheme
        snprintf(
            _env->rodsAuthScheme,
            sizeof( _env->rodsAuthScheme ),
            "native" );

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_SESSION_ENVIRONMENT_FILE, configFileName, LONG_NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_USER_NAME, _env->rodsUserName, NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_HOST, _env->rodsHost, NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_HOME, _env->rodsHome, MAX_NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_CWD, _env->rodsCwd, MAX_NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_AUTHENTICATION_SCHEME, _env->rodsAuthScheme, NAME_LEN);

        capture_integer_property(
            irods::KW_CFG_IRODS_PORT,
            _env->rodsPort );

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_DEFAULT_RESOURCE, _env->rodsDefResource, NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_ZONE, _env->rodsZone, NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_CLIENT_SERVER_POLICY, _env->rodsClientServerPolicy, LONG_NAME_LEN);

        // Requesting negotiation is the standard now. There's no point in allowing
        // users to change the value of the option, therefore, it is now hard-coded
        // into the library.
        std::strncpy(
            _env->rodsClientServerNegotiation, REQ_SVR_NEG, sizeof(RodsEnvironment::rodsClientServerNegotiation) - 1);

        capture_integer_property(
            irods::KW_CFG_IRODS_ENCRYPTION_KEY_SIZE,
            _env->rodsEncryptionKeySize );

        capture_integer_property(
            irods::KW_CFG_IRODS_ENCRYPTION_SALT_SIZE,
            _env->rodsEncryptionSaltSize );

        capture_integer_property(
            irods::KW_CFG_IRODS_ENCRYPTION_NUM_HASH_ROUNDS,
            _env->rodsEncryptionNumHashRounds );

        capture_string_property(irods::KW_CFG_IRODS_ENCRYPTION_ALGORITHM,
                                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                                _env->rodsEncryptionAlgorithm,
                                HEADER_TYPE_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_DEFAULT_HASH_SCHEME, _env->rodsDefaultHashScheme, NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_MATCH_HASH_POLICY, _env->rodsMatchHashPolicy, NAME_LEN);

        _env->rodsLogLevel = 0;
        int status = capture_integer_property(
                         irods::KW_CFG_IRODS_LOG_LEVEL,
                         _env->rodsLogLevel );
        if ( status == 0 && _env->rodsLogLevel > 0 ) {
            if( _env->rodsLogLevel < LOG_SYS_FATAL ) {
                _env->rodsLogLevel = LOG_SYS_FATAL;
            }
            rodsLogLevel( _env->rodsLogLevel );
        }

        memset( _env->rodsAuthFile, 0, sizeof( _env->rodsAuthFile ) );
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        status = capture_string_property(irods::KW_CFG_IRODS_AUTHENTICATION_FILE, _env->rodsAuthFile, LONG_NAME_LEN);
        if ( status == 0 ) {
            rstrcpy(
                authFileName,
                _env->rodsAuthFile,
                LONG_NAME_LEN - 1 );
        }

        // legacy ssl environment variables
        capture_string_property(irods::KW_CFG_IRODS_SSL_CA_CERTIFICATE_PATH,
                                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                                _env->irodsSSLCACertificatePath,
                                MAX_NAME_LEN);

        capture_string_property(irods::KW_CFG_IRODS_SSL_CA_CERTIFICATE_FILE,
                                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                                _env->irodsSSLCACertificateFile,
                                MAX_NAME_LEN);

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_SSL_VERIFY_SERVER, _env->irodsSSLVerifyServer, MAX_NAME_LEN);

        capture_integer_property(
            irods::KW_CFG_IRODS_MAX_SIZE_FOR_SINGLE_BUFFER,
            _env->irodsMaxSizeForSingleBuffer );

        capture_integer_property(
            irods::KW_CFG_IRODS_DEF_NUMBER_TRANSFER_THREADS,
            _env->irodsDefaultNumberTransferThreads );

        capture_integer_property(
            irods::KW_CFG_IRODS_TRANS_BUFFER_SIZE_FOR_PARA_TRANS,
            _env->irodsTransBufferSizeForParaTrans );

        capture_integer_property(
            irods::KW_CFG_IRODS_CONNECTION_POOL_REFRESH_TIME,
            _env->irodsConnectionPoolRefreshTime );

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        capture_string_property(irods::KW_CFG_IRODS_PLUGIN_DIRECTORY, _env->irodsPluginDirectory, MAX_NAME_LEN);

        // If the configuration is not set for the TCP keepalive options, set the value to something invalid. This
        // indicates that we should not set the option on the socket, which will allow the socket to use the kernel
        // configuration.
        status =
            capture_integer_property(irods::KW_CFG_IRODS_TCP_KEEPALIVE_INTVL_IN_SECONDS, _env->tcp_keepalive_intvl);
        if (status < 0) {
            _env->tcp_keepalive_intvl = -1;
        }

        status = capture_integer_property(irods::KW_CFG_IRODS_TCP_KEEPALIVE_PROBES, _env->tcp_keepalive_probes);
        if (status < 0) {
            _env->tcp_keepalive_probes = -1;
        }

        status = capture_integer_property(irods::KW_CFG_IRODS_TCP_KEEPALIVE_TIME_IN_SECONDS, _env->tcp_keepalive_time);
        if (status < 0) {
            _env->tcp_keepalive_time = -1;
        }

        return 0;
    }


    static
    void capture_string_env_var(
        const std::string& _key,
        char*              _val ) {
        char* env = getenv(
                        irods::to_env( _key ).c_str() );
        if ( env ) {
            strncpy(
                _val,
                env,
                strlen( env ) + 1 );
            rodsLog(
                LOG_DEBUG,
                "captured env [%s]-[%s]",
                _key.c_str(),
                _val );
        }

    } // capture_string_env_var

    static
    void capture_integer_env_var(
        const std::string& _key,
        int&               _val ) {
        char* env = getenv(
                        irods::to_env( _key ).c_str() );
        if ( env ) {
            _val = atoi( env );
            rodsLog(
                LOG_DEBUG,
                "captured env [%s]-[%d]",
                _key.c_str(),
                _val );
        }

    } // capture_integer_env_var

    int
    get_legacy_ssl_variables(
        rodsEnv* _env ) {
        if ( !_env ) {
            rodsLog(
                LOG_ERROR,
                "get_legacy_ssl_variables - null env pointer" );
            return SYS_INVALID_INPUT_PARAM;

        }

        char* val = 0;

        val = getenv( "irodsSSLCACertificatePath" );
        if ( val ) {
            snprintf(
                _env->irodsSSLCACertificatePath,
                sizeof( _env->irodsSSLCACertificatePath ),
                "%s", val );
        }

        val = getenv( "irodsSSLCACertificateFile" );
        if ( val ) {
            snprintf(
                _env->irodsSSLCACertificateFile,
                sizeof( _env->irodsSSLCACertificateFile ),
                "%s", val );
        }

        val = getenv( "irodsSSLVerifyServer" );
        if ( val ) {
            snprintf(
                _env->irodsSSLVerifyServer,
                sizeof( _env->irodsSSLVerifyServer ),
                "%s", val );
        }

        return 0;

    } // get_legacy_ssl_variables

    int
    getRodsEnvFromEnv(
        rodsEnv* _env ) {
        if ( !_env ) {
            return SYS_INVALID_INPUT_PARAM;
        }

        int status = get_legacy_ssl_variables( _env );
        if ( status < 0 ) {
            return status;

        }

        std::string env_var = irods::KW_CFG_IRODS_USER_NAME;
        capture_string_env_var(
            env_var,
            _env->rodsUserName );

        env_var = irods::KW_CFG_IRODS_HOST;
        capture_string_env_var(
            env_var,
            _env->rodsHost );

        env_var = irods::KW_CFG_IRODS_PORT;
        capture_integer_env_var(
            env_var,
            _env->rodsPort );

        env_var = irods::KW_CFG_IRODS_HOME;
        capture_string_env_var(
            env_var,
            _env->rodsHome );

        env_var = irods::KW_CFG_IRODS_CWD;
        capture_string_env_var(
            env_var,
            _env->rodsCwd );

        env_var = irods::KW_CFG_IRODS_AUTHENTICATION_SCHEME;
        capture_string_env_var(
            env_var,
            _env->rodsAuthScheme );

        env_var = irods::KW_CFG_IRODS_DEFAULT_RESOURCE;
        capture_string_env_var(
            env_var,
            _env->rodsDefResource );

        env_var = irods::KW_CFG_IRODS_ZONE;
        capture_string_env_var(
            env_var,
            _env->rodsZone );

        env_var = irods::KW_CFG_IRODS_CLIENT_SERVER_POLICY;
        capture_string_env_var(
            env_var,
            _env->rodsClientServerPolicy );

        // Requesting negotiation is the standard now. There's no point in allowing
        // users to change the value of the option, therefore, it is now hard-coded
        // into the library.
        std::strncpy(
            _env->rodsClientServerNegotiation, REQ_SVR_NEG, sizeof(RodsEnvironment::rodsClientServerNegotiation) - 1);

        env_var = irods::KW_CFG_IRODS_ENCRYPTION_KEY_SIZE;
        capture_integer_env_var(
            env_var,
            _env->rodsEncryptionKeySize );

        env_var = irods::KW_CFG_IRODS_ENCRYPTION_SALT_SIZE;
        capture_integer_env_var(
            env_var,
            _env->rodsEncryptionSaltSize );

        env_var = irods::KW_CFG_IRODS_ENCRYPTION_NUM_HASH_ROUNDS;
        capture_integer_env_var(
            env_var,
            _env->rodsEncryptionNumHashRounds );

        env_var = irods::KW_CFG_IRODS_ENCRYPTION_ALGORITHM;
        capture_string_env_var(
            env_var,
            _env->rodsEncryptionAlgorithm );

        env_var = irods::KW_CFG_IRODS_DEFAULT_HASH_SCHEME;
        capture_string_env_var(
            env_var,
            _env->rodsDefaultHashScheme );

        env_var = irods::KW_CFG_IRODS_MATCH_HASH_POLICY;
        capture_string_env_var(
            env_var,
            _env->rodsMatchHashPolicy );

        _env->rodsLogLevel = 0;
        env_var = irods::KW_CFG_IRODS_LOG_LEVEL;
        capture_integer_env_var(
            env_var,
            _env->rodsLogLevel );
        if( _env->rodsLogLevel ) {
            if( _env->rodsLogLevel < LOG_SYS_FATAL ) {
                _env->rodsLogLevel = LOG_SYS_FATAL;
            }

            rodsLogLevel( _env->rodsLogLevel );
        }

        memset( _env->rodsAuthFile, 0, sizeof( _env->rodsAuthFile ) );
        env_var = irods::KW_CFG_IRODS_AUTHENTICATION_FILE;
        capture_string_env_var(
            env_var,
            _env->rodsAuthFile );
        if ( strlen( _env->rodsAuthFile ) > 0 ) {
            rstrcpy( authFileName, _env->rodsAuthFile, LONG_NAME_LEN );

        }

        // legacy ssl environment variables
        env_var = irods::KW_CFG_IRODS_SSL_CA_CERTIFICATE_PATH;
        capture_string_env_var(
            env_var,
            _env->irodsSSLCACertificatePath );
        env_var = irods::KW_CFG_IRODS_SSL_CA_CERTIFICATE_FILE;
        capture_string_env_var(
            env_var,
            _env->irodsSSLCACertificateFile );
        env_var = irods::KW_CFG_IRODS_SSL_VERIFY_SERVER;
        capture_string_env_var(
            env_var,
            _env->irodsSSLVerifyServer );
        env_var = irods::KW_CFG_IRODS_SSL_VERIFY_SERVER;
        capture_string_env_var(
            env_var,
            _env->irodsSSLVerifyServer );
        env_var = irods::KW_CFG_IRODS_SSL_CERTIFICATE_CHAIN_FILE;

        env_var = irods::KW_CFG_IRODS_MAX_SIZE_FOR_SINGLE_BUFFER;
        capture_integer_env_var(
            env_var,
            _env->irodsMaxSizeForSingleBuffer );

        env_var = irods::KW_CFG_IRODS_DEF_NUMBER_TRANSFER_THREADS;
        capture_integer_env_var(
            env_var,
            _env->irodsDefaultNumberTransferThreads );

        env_var = irods::KW_CFG_IRODS_TRANS_BUFFER_SIZE_FOR_PARA_TRANS;
        capture_integer_env_var(
            env_var,
            _env->irodsTransBufferSizeForParaTrans );

        env_var = irods::KW_CFG_IRODS_PLUGIN_DIRECTORY;
        capture_string_env_var(env_var, _env->irodsPluginDirectory);

        capture_integer_env_var(irods::KW_CFG_IRODS_TCP_KEEPALIVE_INTVL_IN_SECONDS, _env->tcp_keepalive_intvl);
        capture_integer_env_var(irods::KW_CFG_IRODS_TCP_KEEPALIVE_PROBES, _env->tcp_keepalive_probes);
        capture_integer_env_var(irods::KW_CFG_IRODS_TCP_KEEPALIVE_TIME_IN_SECONDS, _env->tcp_keepalive_time);

        return 0;
    }

    int printRodsEnv(
        FILE*    _fout ) {
        if( !_fout ) {
            fprintf(
                stderr,
                "printRodsEnv :: null input param(s)\n" );
            return SYS_INTERNAL_NULL_INPUT_ERR;
        }

        try {
            const auto& prop_map = irods::environment_properties::instance().map();

            fprintf(
                    _fout,
                    "irods_version - %d.%d.%d\n",
                    IRODS_VERSION_MAJOR,
                    IRODS_VERSION_MINOR,
                    IRODS_VERSION_PATCHLEVEL);

            for( auto itr = prop_map.cbegin(); itr != prop_map.cend(); ++itr ) {

                try {
                    int val = boost::any_cast< int >( itr->second );
                    fprintf(
                            _fout,
                            "%s - %d\n",
                            itr->first.c_str(),
                            val );
                    continue;
                }
                catch ( const boost::bad_any_cast& ) {
                }

                try {
                    const std::string& val = boost::any_cast< const std::string& >( itr->second );
                    fprintf(
                            _fout,
                            "%s - %s\n",
                            itr->first.c_str(),
                            val.c_str() );
                    continue;
                }
                catch ( const boost::bad_any_cast& ) {
                    fprintf(
                            stderr,
                            "failed to cast %s",
                            itr->first.c_str() );
                }

            } // for itr
        } catch ( const irods::exception& e ) {
            fprintf( stderr,
                "%s",
                e.what() );
            return e.code();
        }

        return 0;

    } // printRodsEnv


    /* build a couple default values from others if appropriate */
    int
    createRodsEnvDefaults( rodsEnv *rodsEnvArg ) {
        if ( strlen( rodsEnvArg->rodsHome ) == 0 &&
                strlen( rodsEnvArg->rodsUserName ) > 0 &&
                strlen( rodsEnvArg->rodsZone ) > 0 ) {
            snprintf( rodsEnvArg->rodsHome,  MAX_NAME_LEN, "/%s/home/%s",
                        rodsEnvArg->rodsZone, rodsEnvArg->rodsUserName );
        }
        if ( strlen( rodsEnvArg->rodsCwd ) == 0 &&
                strlen( rodsEnvArg->rodsHome ) > 0 ) {
            rstrcpy( rodsEnvArg->rodsCwd, rodsEnvArg->rodsHome, MAX_NAME_LEN );
        }

        return 0;
    }


    /*
      find the next delimited token and terminate the string with matching quotes
    */
    char *findNextTokenAndTerm( char *inPtr ) {
        char *myPtr = 0;
        char *savePtr = 0;
        char *nextPtr = 0;
        int whiteSpace = 0;
        myPtr = inPtr;
        whiteSpace = 1;
        for ( ;; myPtr++ ) {
            if ( *myPtr == ' ' || *myPtr == '=' ) {
                continue;
            }
            if ( *myPtr == '"' && whiteSpace ) {
                myPtr++;
                savePtr = myPtr;
                for ( ;; ) {
                    if ( *myPtr == '"' ) {
                        nextPtr = myPtr + 1;
                        if ( *nextPtr == ' ' || *nextPtr == '\n'  || *nextPtr == '\0' ) {
                            /* embedded "s are OK */
                            *myPtr = '\0';
                            return savePtr;
                        }
                    }
                    if ( *myPtr == '\n' ) {
                        *myPtr = '\0';
                    }
                    if ( *myPtr == '\0' ) {
                        /* terminated without a corresponding ", so backup and
                           put the starting one back */
                        savePtr--;
                        *savePtr = '"';
                        return savePtr;
                    }
                    myPtr++;
                }
            }
            if ( *myPtr == '\'' && whiteSpace ) {
                myPtr++;
                savePtr = myPtr;
                for ( ;; ) {
                    if ( *myPtr == '\'' ) {
                        nextPtr = myPtr + 1;
                        if ( *nextPtr == ' ' || *nextPtr == '\n'  || *nextPtr == '\0' ) {
                            /* embedded 's are OK */
                            *myPtr = '\0';
                            return savePtr;
                        }
                    }
                    if ( *myPtr == '\n' ) {
                        *myPtr = '\0';
                    }
                    if ( *myPtr == '\0' ) {
                        /* terminated without a corresponding ", so backup and
                           put the starting one back */
                        savePtr--;
                        *savePtr = '\'';
                        return savePtr;
                    }
                    myPtr++;
                }
            }
            if ( whiteSpace ) {
                savePtr = myPtr;
            }
            whiteSpace = 0;
            if ( *myPtr == '\n' ) {
                *myPtr = '\0';
            }
            if ( *myPtr == '\r' ) {
                *myPtr = '\0';
            }
            if ( *myPtr == '\0' ) {
                return savePtr;
            }
        }
    }

} // extern "C"
