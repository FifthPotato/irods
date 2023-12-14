/// \file

#include "irods/icatHighLevelRoutines.hpp"

#include "irods/irods_log.hpp"
#include "irods/irods_re_structs.hpp"

#include <boost/process.hpp>
#include <chrono>
#include <curl/curl.h>
#include <curl/easy.h>
#include <fmt/format.h>
#include <iomanip>
#include <pwd.h>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>


#include <boost/exception/diagnostic_information.hpp> 
namespace
{
    using log_re = irods::experimental::log::rule_engine;
} // anonymous namespace

/*
 * For an SMTP example using the multi interface please see smtp-multi.c.
 */
 
/* The libcurl options want plain addresses, the viewable headers in the mail
 * can get a full name as well.
 */
 

int prep_headers_curl_smtp(const char *to, const char *from, const char *subject, FILE *storage) {
  try {
  auto timeType = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  auto timeStruct = localtime(&timeType);
  std::stringstream ss{}; 
  ss << std::put_time(timeStruct, "%a, %d %b %Y %T %z");
  std::string headers = fmt::format("Date: {0}\r\nTo: {1} <{1}>\r\nFrom: {2} <{2}>\r\nSubject: {3}\r\n\r\n", ss.str(), to, from, subject);
  log_re::error(headers);
  fwrite(headers.c_str(), sizeof(char), headers.size(), storage);
  } catch(...) {
    log_re::error(boost::current_exception_diagnostic_information());
  }
  return ferror(storage);
}
 
int send_curl_smtp(const char *to, const char *from, const char *subject, FILE *storage)
{
  CURL *curl;
  CURLcode res = CURLE_OK;
  struct curl_slist *recipients = NULL;
  curl = curl_easy_init();
  if(curl) {
    /* This is the URL for your mailserver */
    curl_easy_setopt(curl, CURLOPT_URL, "smtp://localhost");
 
    /* Note that this option is not strictly required, omitting it will result
     * in libcurl sending the MAIL FROM command with empty sender data. All
     * autoresponses should have an empty reverse-path, and should be directed
     * to the address in the reverse-path which triggered them. Otherwise,
     * they could cause an endless loop. See RFC 5321 Section 4.5.5 for more
     * details.
     */
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from);
 
    /* Add two recipients, in this particular case they correspond to the
     * To: and Cc: addressees in the header, but they could be any kind of
     * recipient. */
    recipients = curl_slist_append(recipients, to);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
 
    /* We are using a callback function to specify the payload (the headers and
     * body of the message). You could just use the CURLOPT_READDATA option to
     * specify a FILE pointer to read from. */
    curl_easy_setopt(curl, CURLOPT_READDATA, (void*)storage);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
 
    /* Send the message */
    res = curl_easy_perform(curl);
 
    /* Check for errors */
    if(res != CURLE_OK)
      log_re::error("curl_easy_perform() failed: {}",
              curl_easy_strerror(res));
 
    /* Free the list of recipients */
    curl_slist_free_all(recipients);
 
    /* curl will not send the QUIT command until you call cleanup, so you
     * should be able to reuse this connection for additional messages
     * (setting CURLOPT_MAIL_FROM and CURLOPT_MAIL_RCPT as required, and
     * calling curl_easy_perform() again. It may not be a good idea to keep
     * the connection open for a long time though (more than a few minutes may
     * result in the server timing out the connection), and you do want to
     * clean up in the end.
     */
    curl_easy_cleanup(curl);
  }
 
  return (int)res;
}

/**
 * \fn msiSendMail(msParam_t* xtoAddr, msParam_t* xsubjectLine, msParam_t* xbody, ruleExecInfo_t *)
 *
 * \brief Sends email
 *
 * \module core
 *
 * \since pre-2.1
 *
 *
 * \note   This microservice sends e-mail using the mail command in the unix system. No attachments are supported. The
 *sender of the e-mail is the unix user-id running the irodsServer.
 *
 * \usage See clients/icommands/test/rules/
 *
 * \param[in] xtoAddr - a msParam of type STR_MS_T which is an address of the receiver.
 * \param[in] xsubjectLine - a msParam of type STR_MS_T which is a subject of the message.
 * \param[in] xbody - a msParam of type STR_MS_T which is a body of the message.
 * \param[in,out] - The RuleExecInfo structure that is automatically
 *    handled by the rule engine. The user does not include rei as a
 *    parameter in the rule invocation.
 *
 * \DolVarDependence none
 * \DolVarModified none
 * \iCatAttrDependence none
 * \iCatAttrModified none
 * \sideeffect An e-mail sent to the specified recipient.
 *
 * \return integer
 * \retval 0 on success
 * \pre none
 * \post none
 * \sa none
 **/
int msiSendMail( msParam_t* xtoAddr, msParam_t* xsubjectLine, msParam_t* xbody, ruleExecInfo_t* ) {

    const char * toAddr = ( char * ) xtoAddr->inOutStruct;
    const char * subjectLine = xsubjectLine->inOutStruct ? ( char * ) xsubjectLine->inOutStruct : "";
    const char * body = xbody->inOutStruct ? ( char * ) xbody->inOutStruct : "";

    if ( int status = checkStringForEmailAddress( toAddr ) ) {
        rodsLog( LOG_NOTICE, "checkStringForEmailAddress failed for [%s]", toAddr );
        return status;
    }
    if ( int status = checkStringForSystem( subjectLine ) ) {
        rodsLog( LOG_NOTICE, "checkStringForSystem failed for [%s]", subjectLine );
        return status;
    }

    char fName[] = "/tmp/irods_mailFileXXXXXXXXXX";
    int fd = mkstemp( fName );
    if (fd < 0) {
        rodsLog(LOG_ERROR, "msiSendMail: mkstemp() failed [%s] %d - %d", fName, fd, errno);
        return FILE_OPEN_ERR;
    }

    FILE* file = fdopen(fd, "w+");
    if ( file == NULL ) {
        rodsLog( LOG_ERROR, "failed to create file [%s] errno %d", fName, errno );
        return FILE_CREATE_ERROR;
    }
    int var = 0;
    if(var = prep_headers_curl_smtp(toAddr, "admin@irods.org", subjectLine, file)) {
        log_re::error("{}: Failed to prep headers with file error {}", __func__, var);
    }
    const char * t1 = body;
    while ( const char * t2 = strstr( t1, "\\n" ) ) {
        fwrite( t1, sizeof( *t1 ), t2 - t1, file );
        fprintf( file, "\n" );
        t1 = t2 + 2;
    }
    fprintf( file, "%s\n", t1 );
    char * mailStr = ( char* )malloc( strlen( toAddr ) + strlen( subjectLine ) + 100 );
    if ( mailStr == NULL ) {
        return SYS_MALLOC_ERR;
    }
    fseek(file, 0, SEEK_SET);
    send_curl_smtp(toAddr, "admin@irods.org", subjectLine, file);
    fclose( file );
/*
    namespace bp = boost::process;
    std::error_code ec{0, std::system_category()};
    int ret = bp::system(bp::search_path("mail"), "-s", subjectLine, toAddr, bp::std_in < fName, ec);
    if (ret || ec) {
        log_re::error("{}: mail command return code: {}, error code: {}", __func__, ret, ec.value());
    }
    sprintf( mailStr, "rm %s", fName );
    ret = system( mailStr );
    if ( ret ) {
        irods::log( ERROR( ret, "mailStr command returned non-zero status" ) );
    }
*/
    free( mailStr );
    return 0;
}
