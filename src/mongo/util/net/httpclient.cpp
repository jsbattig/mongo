// httpclient.cpp

/*    Copyright 2009 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/util/net/httpclient.h"

#include "mongo/bson/util/builder.h"
#include "mongo/config.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/message.h"
#include "mongo/util/net/message_port.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/net/ssl_manager.h"

namespace mongo {

    using std::string;
    using std::stringstream;

    //#define HD(x) cout << x << endl;
#define HD(x)


    int HttpClient::get( const std::string& url , Result * result ) {
        return _go( "GET" , url , 0 , result );
    }

    int HttpClient::post( const std::string& url , const std::string& data , Result * result ) {
        return _go( "POST" , url , data.c_str() , result );
    }

    int HttpClient::_go( const char * command , string url , const char * body , Result * result ) {
        bool ssl = false;
        if ( url.find( "https://" ) == 0 ) {
            ssl = true;
            url = url.substr( 8 );
        }
        else {
            uassert( 10271 ,  "invalid url" , url.find( "http://" ) == 0 );
            url = url.substr( 7 );
        }

        string host , path;
        if ( url.find( "/" ) == string::npos ) {
            host = url;
            path = "/";
        }
        else {
            host = url.substr( 0 , url.find( "/" ) );
            path = url.substr( url.find( "/" ) );
        }


        HD( "host [" << host << "]" );
        HD( "path [" << path << "]" );

        string server = host;
        int port = ssl ? 443 : 80;

        string::size_type idx = host.find( ":" );
        if ( idx != string::npos ) {
            server = host.substr( 0 , idx );
            string t = host.substr( idx + 1 );
            port = atoi( t.c_str() );
        }

        HD( "server [" << server << "]" );
        HD( "port [" << port << "]" );

        string req;
        {
            stringstream ss;
            ss << command << " " << path << " HTTP/1.1\r\n";
            ss << "Host: " << host << "\r\n";
            ss << "Connection: Close\r\n";
            ss << "User-Agent: mongodb http client\r\n";
            if ( body ) {
                ss << "Content-Length: " << strlen( body ) << "\r\n";
            }
            ss << "\r\n";
            if ( body ) {
                ss << body;
            }

            req = ss.str();
        }

        SockAddr addr( server.c_str() , port );
        uassert( 15000 ,  "server socket addr is invalid" , addr.isValid() );
        HD( "addr: " << addr.toString() );

        Socket sock;
        if ( ! sock.connect( addr ) )
            return -1;
        
        if ( ssl ) {
#ifdef MONGO_CONFIG_SSL
            // pointer to global singleton instance
            SSLManagerInterface* mgr = getSSLManager();

            sock.secure(mgr, "");
#else
            uasserted( 15862 , "no ssl support" );
#endif
        }

        {
            const char * out = req.c_str();
            int toSend = req.size();
            sock.send( out , toSend, "_go" );
        }

        char buf[4097];
        int got = sock.unsafe_recv( buf , 4096 );
        buf[got] = 0;

        int rc;
        char version[32];
        verify( sscanf( buf , "%s %d" , version , &rc ) == 2 );
        HD( "rc: " << rc );

        StringBuilder sb;
        if ( result )
            sb << buf;

        // SERVER-8864, unsafe_recv will throw when recv returns 0 indicating closed socket.
        try {
            while ( ( got = sock.unsafe_recv( buf , 4096 ) ) > 0) {
                buf[got] = 0;
                if ( result )
                    sb << buf;
            }
        } catch (const SocketException&) {}


        if ( result ) {
            result->_init( rc , sb.str() );
        }

        return rc;
    }

    void HttpClient::Result::_init( int code , string entire ) {
        _code = code;
        _entireResponse = entire;

        while ( true ) {
            size_t i = entire.find( '\n' );
            if ( i == string::npos ) {
                // invalid
                break;
            }

            string h = entire.substr( 0 , i );
            entire = entire.substr( i + 1 );

            if ( h.size() && h[h.size()-1] == '\r' )
                h = h.substr( 0 , h.size() - 1 );

            if ( h.size() == 0 )
                break;

            i = h.find( ':' );
            if ( i != string::npos )
                _headers[h.substr(0,i)] = str::ltrim(h.substr(i+1));
        }

        _body = entire;
    }

}
