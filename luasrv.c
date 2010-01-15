/**
 * luasrv - a HTTPd for lua 
 * (c) 2009-19 Alacner zhang <alacner@gmail.com>
 * This content is released under the MIT License.
*/

#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <stdbool.h>

#include <err.h>
#include <event.h>
#include <evhttp.h>

#define VERSION "1.0.0"

#include "lua.h"
#include "lauxlib.h"
#if ! defined (LUA_VERSION_NUM) || LUA_VERSION_NUM < 501
#include "compat-5.1.h"
#endif

#if LUA_VERSION_NUM < 501
#define luaL_register(a, b, c) luaL_openlib((a), (b), (c), 0)
#endif

#define safe_emalloc(nmemb, size, offset)  malloc((nmemb) * (size) + (offset)) 

//Global Setting
lua_State *L; /* lua state handle */
char *luasrv_settings_rootpath = NULL;
struct evhttp *httpd;
struct evbuffer *buf;
struct evkeyvalq *input_headers;
struct evkeyvalq *output_headers;

static void show_help(void)
{
	char *b = "--------------------------------------------------------------------------------------------------\n"
		  "HTTP lua Service - luasrv v" VERSION "\n\n"
		  "Author: Alacner Zhang (http://alacner.com), E-mail: alacner@gmail.com\n"
		  "\n"
		   "-l <ip_addr>  interface to listen on, default is 0.0.0.0\n"
		   "-p <num>      TCP port number to listen on (default: 1206)\n"
		   "-x <path>     root directory (example: /var/www/html)\n"
		   "-t <second>   timeout for an http request (default: 1)\n"		   
		   "-d            run as a daemon\n"
		   "-h            print this help and exit\n\n"
		   "Use command \"killall luasrv\", \"pkill luasrv\" and \"kill PID of luasrv\" to stop luasrv.\n"
		   "Please note that don't use the command \"pkill -9 luasrv\" and \"kill -9 PID of luasrv\"!\n"
		   "\n"
		   "Please visit \"http://alacner.com/pro/luasrv\" for more help information.\n\n"
		   "--------------------------------------------------------------------------------------------------\n"
		   "\n";
	fprintf(stderr, b, strlen(b));
}

/* 创建多层目录的函数 */
void create_multilayer_dir( char *muldir ) {
    int    i,len;
    char    str[512];
    
    strncpy( str, muldir, 512 );
    len=strlen(str);
    for( i=0; i<len; i++ ) {
        if( str[i]=='/' ) {
            str[i] = '\0';
            //判断此目录是否存在,不存在则创建
            if( access(str, F_OK)!=0 ) {
                mkdir( str, 0777 );
            }
            str[i]='/';
        }
    }
    if( len>0 && access(str, F_OK)!=0 ) {
        mkdir( str, 0777 );
    }

    return;
}

static int luaM_mkdir (lua_State *L) {
	const char *muldir = luaL_optstring(L, 1, NULL);
	create_multilayer_dir((char *)muldir);
	if (access(muldir, W_OK) == 0) {
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;	
}

#define NUL  '\0'
#define MICRO_IN_SEC 1000000.00
#define SEC_IN_MIN 60
static int luaM_microtime (lua_State *L) {
	struct timeval tp = {0};
	int get_as_float = luaL_optnumber(L, 1, 0);

	if (gettimeofday(&tp, NULL)) {
		lua_pushboolean(L, 0);
    }

    if (get_as_float) {
        lua_pushnumber(L, (double)(tp.tv_sec + tp.tv_usec / MICRO_IN_SEC));
    } else {
		lua_pushfstring(L, "%f %d", tp.tv_usec / MICRO_IN_SEC, tp.tv_sec);
	}

	return 1;	
}

static int luaM_print (lua_State *L) {
	const char *str = luaL_optstring(L, 1, NULL);
	evbuffer_add_printf(buf, "%s", str);
	return 0;
}

static int luaM_get_header (lua_State *L) {
	const char *header = luaL_optstring(L, 1, NULL);
	const char *header_data = evhttp_find_header (input_headers, header);
	lua_pushstring(L, header_data);
	return 1;
}

static int luaM_set_header (lua_State *L) {
	const char *header = luaL_optstring(L, 1, NULL);
	const char *value = luaL_optstring(L, 2, NULL);
	evhttp_add_header(output_headers, header, value);
	return 0;
}

/* 处理模块 */
void luasrv_handler(struct evhttp_request *req, void *arg)
{
	buf = evbuffer_new();
	input_headers = req->input_headers;
	output_headers = req->output_headers;
	
	// GET
	char *decode_uri = strdup((char*) evhttp_request_uri(req));
	lua_pushstring(L, decode_uri);
	lua_setglobal(L, "GET_DATA");

	// POST
	int post_data_len;
	post_data_len = EVBUFFER_LENGTH(req->input_buffer);

		fprintf(stderr, "\n - %d - \n", post_data_len);
	if (post_data_len > 0) {
		lua_pushstring(L, (char *)EVBUFFER_DATA(req->input_buffer));
		lua_setglobal(L, "POST_DATA");
	}

	evhttp_add_header(req->output_headers, "Server", "luasrv" VERSION);
	evhttp_add_header(req->output_headers, "Keep-Alive", "120");
	
	//evbuffer_add_printf(buf, "%s", "SUCCESS");

	char *script_name = strtok(decode_uri, "?");
	char *query_string = strtok(NULL, "?");

    lua_pushfstring(L, "%s%s", luasrv_settings_rootpath, script_name);
	const char *script_filename = lua_tolstring(L, -1, NULL);
    lua_pop(L, 1);

	struct stat stat_buf;   

	if (lstat(script_filename, &stat_buf) < 0) {  
		fprintf(stderr, "lstat error for %s\r\n", script_filename);  

		evhttp_send_reply(req, HTTP_NOTFOUND, "NOT FOUND", buf);
		/* 内存释放 */
		free(decode_uri);
		evbuffer_free(buf);
		return;
	}  

	if ( !S_ISREG(stat_buf.st_mode)) {//S_ISDIR
		fprintf(stderr, "not found!\n");
		evhttp_send_reply(req, HTTP_NOTFOUND, "NOT FOUND", buf);
		/* 内存释放 */
		free(decode_uri);
		evbuffer_free(buf);
		return;
	}

	lua_newtable(L);

	lua_pushstring(L, "SCRIPT_NAME");
	lua_pushstring(L, script_name);
	lua_rawset(L, -3);

	lua_pushstring(L, "REQUEST_URI");
	lua_pushstring(L, req->uri);
	lua_rawset(L, -3);

	lua_pushstring(L, "REQUEST_TIME");
	lua_pushnumber(L, (int)time((time_t*)NULL));
	lua_rawset(L, -3);

	lua_pushstring(L, "DOCUMENT_ROOT");
	lua_pushstring(L, luasrv_settings_rootpath);
	lua_rawset(L, -3);

	lua_pushstring(L, "SCRIPT_FILENAME");
	lua_pushstring(L, script_filename);
	lua_rawset(L, -3);


	lua_pushstring(L, "REQUEST_METHOD");
	lua_pushnumber(L, req->type);
	lua_rawset(L, -3);

	lua_pushstring(L, "QUERY_STRING");
	lua_pushstring(L, query_string);
	lua_rawset(L, -3);

	lua_pushstring(L, "HTTP_X_FORWARDED_FOR");
	lua_pushstring(L, req->remote_host);
	lua_rawset(L, -3);
	lua_setglobal(L, "SERVER");



	//evbuffer_add_printf(buf, "<br/>%s<br/>", script_filename);

	if (luaL_loadfile(L, "./script/loader.lua") || lua_pcall(L, 0, 0, 0)) {
		fprintf (stderr, "cannot run loader.lua: %s", lua_tostring(L, -1));
		return;
	}

	/* 输出内容给客户端 */
	evhttp_send_reply(req, HTTP_OK, "OK", buf);
	
	/* 内存释放 */
	free(decode_uri);
	evbuffer_free(buf);
}

/* 信号处理 */
static void kill_signal(const int sig) {
	lua_close(L);
    exit(0);
}

int main(int argc, char **argv) {
	int c;
	/* 默认参数设置 */
	char *luasrv_settings_listen = "0.0.0.0";
	int luasrv_settings_port = 1206;
	bool luasrv_settings_daemon = false;
	int luasrv_settings_timeout = 1; /* 单位：秒 */

	L = luaL_newstate();
	luaL_openlibs(L);

	struct luaL_reg driver[] = {
        { "print", luaM_print },
        { "mkdir", luaM_mkdir },
        { "microtime", luaM_microtime },
        { "get_header", luaM_get_header },
        { "set_header", luaM_set_header },
        { NULL, NULL },
    };

	luaL_register (L, "cgi", driver);

	if (luaL_loadfile(L, "./script/lp.lua") || lua_pcall(L, 0, 0, 0)) { /* load the compile template functions */
		fprintf (stderr, "cannot run lp.lua: %s", lua_tostring(L, -1));
		kill_signal(1);
		return 0;
	}

	//char cwd_buf[255];
	//getcwd(cwd_buf,sizeof(cwd_buf));

	//fprintf(stderr, cwd_buf);
    /* process arguments */
    while ((c = getopt(argc, argv, "l:p:x:t:dh")) != -1) {
        switch (c) {
        case 'l':
            luasrv_settings_listen = strdup(optarg);
            break;
        case 'p':
            luasrv_settings_port = atoi(optarg);
            break;
        case 'x':
            luasrv_settings_rootpath = strdup(optarg); /* luasrv根路径 */
			if (access(luasrv_settings_rootpath, R_OK) != 0) { /* 如果目录不可写 */
				fprintf(stderr, "luasrv root directory not readable\n");
			}
            break;
        case 't':
            luasrv_settings_timeout = atoi(optarg);
            break;			
        case 'd':
            luasrv_settings_daemon = true;
            break;
		case 'h':
        default:
            show_help();
            return 1;
        }
    }
	
	/* 判断是否加了必填参数 -x */
	if (luasrv_settings_rootpath == NULL) {
		show_help();
		fprintf(stderr, "Attention: Please use the indispensable argument: -x <path>\n\n");		
		kill_signal(1);
	}
	
	/* 如果加了-d参数，以守护进程运行 */
	if (luasrv_settings_daemon == true){
        pid_t pid;

        /* Fork off the parent process */       
        pid = fork();
        if (pid < 0) {
                exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }
	}
	
	/* 忽略Broken Pipe信号 */
	signal (SIGPIPE, SIG_IGN);
	
	/* 处理kill信号 */
	signal (SIGINT, kill_signal);
	signal (SIGKILL, kill_signal);
	signal (SIGQUIT, kill_signal);
	signal (SIGTERM, kill_signal);
	signal (SIGHUP, kill_signal);
	
	/* 请求处理部分 */


    event_init();
    httpd = evhttp_start(luasrv_settings_listen, luasrv_settings_port);
    evhttp_set_timeout(httpd, luasrv_settings_timeout);

    /* Set a callback for requests to "/specific". */
    /* evhttp_set_cb(httpd, "/select", select_handler, NULL); */

    /* Set a callback for all other requests. */
    evhttp_set_gencb(httpd, luasrv_handler, NULL);

    event_dispatch();

    /* Not reached in this code as it is now. */
    evhttp_free(httpd);

	return 0;
}
