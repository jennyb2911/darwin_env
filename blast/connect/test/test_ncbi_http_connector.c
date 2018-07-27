/*  $Id: test_ncbi_http_connector.c,v 6.6 2001/01/11 16:42:50 lavr Exp $
 * ===========================================================================
 *
 *                            PUBLIC DOMAIN NOTICE
 *               National Center for Biotechnology Information
 *
 *  This software/database is a "United States Government Work" under the
 *  terms of the United States Copyright Act.  It was written as part of
 *  the author's official duties as a United States Government employee and
 *  thus cannot be copyrighted.  This software/database is freely available
 *  to the public for use. The National Library of Medicine and the U.S.
 *  Government have not placed any restriction on its use or reproduction.
 *
 *  Although all reasonable efforts have been taken to ensure the accuracy
 *  and reliability of the software and data, the NLM and the U.S.
 *  Government do not and cannot warrant the performance or results that
 *  may be obtained by using this software or data. The NLM and the U.S.
 *  Government disclaim all warranties, express or implied, including
 *  warranties of performance, merchantability or fitness for any particular
 *  purpose.
 *
 *  Please cite the author in any work or product based on this material.
 *
 * ===========================================================================
 *
 * Author:  Denis Vakatov
 *
 * File Description:
 *   Standard test for the HTTP-based CONNECTOR
 *
 * --------------------------------------------------------------------------
 * $Log: test_ncbi_http_connector.c,v $
 * Revision 6.6  2001/01/11 16:42:50  lavr
 * Registry Get/Set methods got the 'user_data' argument, forgotten earlier
 *
 * Revision 6.5  2001/01/08 23:48:51  lavr
 * REQ_METHOD "any" added to SConnNetInfo
 *
 * Revision 6.4  2000/11/15 17:29:52  vakatov
 * Fixed path to the test CGI application.
 *
 * Revision 6.3  2000/09/27 15:58:17  lavr
 * Registry entries adjusted
 *
 * Revision 6.2  2000/05/30 23:25:03  vakatov
 * Cosmetic fix for the C++ compilation
 *
 * Revision 6.1  2000/04/21 19:57:02  vakatov
 * Initial revision
 *
 * ==========================================================================
 */


#if defined(NDEBUG)
#  undef NDEBUG
#endif 

#include "ncbi_conntest.h"
#include <connect/ncbi_http_connector.h>
#include <connect/ncbi_util.h>
#include <string.h>


/* Hard-coded pseudo-registry getter
 */

#define TEST_HOST            "ray.nlm.nih.gov"
#define TEST_PORT            "6224"
#define TEST_PATH            "/tools/vakatov/con_url.cgi"
#define TEST_ARGS            "arg1+arg2+arg3"
#define TEST_DEBUG_PRINTOUT  "yes"
#define TEST_REQ_METHOD      "any"


#if defined(__cplusplus)
extern "C" {
    static void s_REG_Get(void* user_data,
                          const char* section, const char* name,
                          char* value, size_t value_size);
}
#endif /* __cplusplus */

static void s_REG_Get
(void*       user_data,
 const char* section,
 const char* name,
 char*       value,
 size_t      value_size)
{
    if (strcmp(section, DEF_CONN_REG_SECTION) != 0) {
        assert(0);
        return;
    }

#define X_GET_VALUE(x_name, x_value) \
  if (strcmp(name, x_name) == 0) { \
      strncpy(value, x_value, value_size); \
      value[value_size - 1] = '\0'; \
      return; \
  }

    X_GET_VALUE(REG_CONN_HOST,           TEST_HOST);
    X_GET_VALUE(REG_CONN_PORT,           TEST_PORT);
    X_GET_VALUE(REG_CONN_PATH,           TEST_PATH);
    X_GET_VALUE(REG_CONN_ARGS,           TEST_ARGS);
    X_GET_VALUE(REG_CONN_REQ_METHOD,     TEST_REQ_METHOD);
    X_GET_VALUE(REG_CONN_DEBUG_PRINTOUT, TEST_DEBUG_PRINTOUT);
}


/*****************************************************************************
 *  MAIN
 */

int main(void)
{
    STimeout    timeout;
    CONNECTOR   connector;
    FILE*       data_file;
    const char* user_header = 0;
    THCC_Flags  flags;

    /* Log and data-log streams */
    CORE_SetLOGFILE(stderr, 0/*false*/);
    data_file = fopen("test_ncbi_http_connector.log", "wb");
    assert(data_file);

    /* Tune to the test URL using hard-coded pseudo-registry */
    CORE_SetREG( REG_Create(0, s_REG_Get, 0, 0, 0) );

    /* Connection timeout */
    timeout.sec  = 5;
    timeout.usec = 123456;

    /* Printout all socket traffic */
    /* SOCK_SetDataLoggingAPI(eOn); */

    /* Run the tests */
    flags = fHCC_KeepHeader | fHCC_UrlCodec | fHCC_UrlEncodeArgs;
    connector = HTTP_CreateConnector(0, user_header, flags);
    CONN_TestConnector(connector, &timeout, data_file,
                       fTC_SingleBouncePrint);

    flags = 0;
    connector = HTTP_CreateConnector(0, user_header, flags);
    CONN_TestConnector(connector, &timeout, data_file,
                       fTC_SingleBounceCheck);

    flags = fHCC_AutoReconnect;
    connector = HTTP_CreateConnector(0, user_header, flags);
    CONN_TestConnector(connector, &timeout, data_file,
                       fTC_Everything);

    flags = fHCC_AutoReconnect | fHCC_UrlCodec;
    connector = HTTP_CreateConnector(0, user_header, flags);
    CONN_TestConnector(connector, &timeout, data_file,
                       fTC_Everything);

    /* Cleanup and Exit */
    CORE_SetREG(0);
    fclose(data_file);
    CORE_SetLOG(0);
    return 0;
}
