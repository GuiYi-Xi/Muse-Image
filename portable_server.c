#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHUNK 16384
#define MAX_HEADER 65536

typedef struct { SOCKET client; char root[MAX_PATH*4]; } CLIENT_JOB;

static int send_all(SOCKET s,const char*b,int n){int p=0;while(p<n){int k=send(s,b+p,n-p,0);if(k<=0)return 0;p+=k;}return 1;}
static const char*mime(const char*p){const char*e=strrchr(p,'.');if(!e)return"application/octet-stream";if(!_stricmp(e,".html"))return"text/html; charset=utf-8";if(!_stricmp(e,".js"))return"application/javascript; charset=utf-8";if(!_stricmp(e,".json"))return"application/json; charset=utf-8";if(!_stricmp(e,".css"))return"text/css; charset=utf-8";if(!_stricmp(e,".png"))return"image/png";if(!_stricmp(e,".jpg")||!_stricmp(e,".jpeg"))return"image/jpeg";if(!_stricmp(e,".webp"))return"image/webp";return"application/octet-stream";}
static void text_response(SOCKET c,int code,const char*reason,const char*body){char h[1024];int z=(int)strlen(body),n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",code,reason,z);send_all(c,h,n);send_all(c,body,z);}
static int header_value(const char*headers,const char*name,char*out,size_t cap){size_t nl=strlen(name);const char*p=headers;while((p=strstr(p,"\r\n"))){p+=2;if(!_strnicmp(p,name,nl)&&p[nl]==':'){p+=nl+1;while(*p==' '||*p=='\t')p++;const char*e=strstr(p,"\r\n");if(!e)return 0;size_t n=(size_t)(e-p);if(n>=cap)n=cap-1;memcpy(out,p,n);out[n]=0;return 1;}}return 0;}
static wchar_t*widen(const char*s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,NULL,0);wchar_t*w=(wchar_t*)calloc(n,sizeof(wchar_t));if(w)MultiByteToWideChar(CP_UTF8,0,s,-1,w,n);return w;}

static void proxy_request(SOCKET c,const char*method,char*headers,unsigned char*body,DWORD body_len){
 char target[4096],auth[8192],ctype[1024]; if(!header_value(headers,"X-Target-URL",target,sizeof(target))||_strnicmp(target,"https://",8)){text_response(c,400,"Bad Request","Missing or invalid X-Target-URL");return;}
 int has_auth=header_value(headers,"Authorization",auth,sizeof(auth)); int has_ctype=header_value(headers,"Content-Type",ctype,sizeof(ctype));
 wchar_t*wurl=widen(target); URL_COMPONENTS u;memset(&u,0,sizeof(u));u.dwStructSize=sizeof(u);u.dwSchemeLength=(DWORD)-1;u.dwHostNameLength=(DWORD)-1;u.dwUrlPathLength=(DWORD)-1;u.dwExtraInfoLength=(DWORD)-1;
 if(!wurl||!WinHttpCrackUrl(wurl,0,0,&u)){free(wurl);text_response(c,502,"Bad Gateway","Invalid target URL");return;}
 wchar_t host[512],path[4096];DWORD hn=u.dwHostNameLength;if(hn>511)hn=511;wcsncpy(host,u.lpszHostName,hn);host[hn]=0;DWORD pn=0;if(u.dwUrlPathLength){wcsncpy(path,u.lpszUrlPath,u.dwUrlPathLength);pn=u.dwUrlPathLength;}if(u.dwExtraInfoLength){wcsncpy(path+pn,u.lpszExtraInfo,u.dwExtraInfoLength);pn+=u.dwExtraInfoLength;}path[pn]=0;
 HINTERNET ses=WinHttpOpen(L"GPTImagePortable/4.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);HINTERNET con=NULL,req=NULL;DWORD status=502;wchar_t ct[512]=L"application/octet-stream";DWORD ctl=sizeof(ct);
 /* WinHTTP defaults can time out long image edits at about 30 seconds. Keep connect bounded, but allow a 20-minute response. */
 if(ses)WinHttpSetTimeouts(ses,30000,30000,30000,1200000);
 /* The gateway intermittently returns malformed/empty replies to long multipart edits over negotiated HTTP/2 (WinHTTP 12152). Force HTTP/1.1 for compatibility. */
 HTTP_VERSION_INFO http11={1,1};
 if(ses)WinHttpSetOption(ses,WINHTTP_OPTION_HTTP_VERSION,&http11,sizeof(http11));
 if(ses)con=WinHttpConnect(ses,host,u.nPort,0);wchar_t*wm=widen(method);if(con)req=WinHttpOpenRequest(con,wm,path,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);free(wm);
 wchar_t extra[8192];extra[0]=0;if(has_auth){wchar_t*wa=widen(auth);_snwprintf(extra,8191,L"Authorization: %ls\r\n",wa);free(wa);}if(has_ctype){wchar_t*wc=widen(ctype);size_t used=wcslen(extra);_snwprintf(extra+used,8191-used,L"Content-Type: %ls\r\n",wc);free(wc);}
 BOOL ok=req&&WinHttpSendRequest(req,extra[0]?extra:WINHTTP_NO_ADDITIONAL_HEADERS,(DWORD)-1L,body_len?body:WINHTTP_NO_REQUEST_DATA,body_len,body_len,0)&&WinHttpReceiveResponse(req,NULL);
 if(ok){DWORD sl=sizeof(status);WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&sl,NULL);WinHttpQueryHeaders(req,WINHTTP_QUERY_CONTENT_TYPE,NULL,ct,&ctl,NULL);}
 if(!ok){DWORD err=GetLastError();char msg[256];snprintf(msg,sizeof(msg),"Upstream request failed (WinHTTP error %lu)",(unsigned long)err);text_response(c,502,"Bad Gateway",msg);}
 else{char ctu[1024];WideCharToMultiByte(CP_UTF8,0,ct,-1,ctu,sizeof(ctu),NULL,NULL);char h[2048];int n=snprintf(h,sizeof(h),"HTTP/1.1 %lu Upstream\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",(unsigned long)status,ctu);send_all(c,h,n);unsigned char b[CHUNK];DWORD got=0;while(WinHttpReadData(req,b,sizeof(b),&got)&&got){if(!send_all(c,(char*)b,(int)got))break;}}
 if(req)WinHttpCloseHandle(req);if(con)WinHttpCloseHandle(con);if(ses)WinHttpCloseHandle(ses);free(wurl);
}

static void handle(SOCKET c,const char*root){
 unsigned char*req=(unsigned char*)malloc(MAX_HEADER);int used=0,k;char*end=NULL;while(used<MAX_HEADER-1){k=recv(c,(char*)req+used,MAX_HEADER-1-used,0);if(k<=0){free(req);return;}used+=k;req[used]=0;end=strstr((char*)req,"\r\n\r\n");if(end)break;}if(!end){free(req);text_response(c,431,"Too Large","Headers too large");return;}
 char method[16],target[4096],ver[32];if(sscanf((char*)req,"%15s %4095s %31s",method,target,ver)!=3){free(req);text_response(c,400,"Bad Request","Bad Request");return;}
 int header_len=(int)(end-(char*)req)+4;char clbuf[64];DWORD total=header_value((char*)req,"Content-Length",clbuf,sizeof(clbuf))?(DWORD)strtoul(clbuf,NULL,10):0;
 unsigned char*body=NULL;if(total){body=(unsigned char*)malloc(total);if(!body){free(req);text_response(c,500,"Error","Out of memory");return;}DWORD have=(DWORD)(used-header_len);if(have>total)have=total;memcpy(body,req+header_len,have);while(have<total){k=recv(c,(char*)body+have,total-have,0);if(k<=0)break;have+=k;}if(have<total){free(body);free(req);text_response(c,400,"Bad Request","Incomplete request body");return;}}
 if(!strncmp(target,"/proxy",6)){proxy_request(c,method,(char*)req,body,total);free(body);free(req);return;}free(body);
 if(_stricmp(method,"GET")&&_stricmp(method,"HEAD")){free(req);text_response(c,405,"Method Not Allowed","Method Not Allowed");return;}char*q=strchr(target,'?');if(q)*q=0;if(!strcmp(target,"/"))strcpy(target,"/index.html");if(strstr(target,"..")||strchr(target,'\\')){free(req);text_response(c,403,"Forbidden","Forbidden");return;}for(char*p=target;*p;p++)if(*p=='/')*p='\\';char full[MAX_PATH*4];snprintf(full,sizeof(full),"%s%s",root,target);FILE*f=fopen(full,"rb");if(!f){free(req);text_response(c,404,"Not Found","Not Found");return;}_fseeki64(f,0,SEEK_END);__int64 sz=_ftelli64(f);_fseeki64(f,0,SEEK_SET);char h[1024];int hn=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",mime(full),(long long)sz);send_all(c,h,hn);if(_stricmp(method,"HEAD")){char b[CHUNK];while((k=(int)fread(b,1,sizeof(b),f))>0)if(!send_all(c,b,k))break;}fclose(f);free(req);
}
static DWORD WINAPI client_worker(LPVOID arg){CLIENT_JOB*j=(CLIENT_JOB*)arg;handle(j->client,j->root);shutdown(j->client,SD_BOTH);closesocket(j->client);free(j);return 0;}

int main(void){SetConsoleOutputCP(CP_UTF8);char root[MAX_PATH*4];DWORD n=GetModuleFileNameA(NULL,root,sizeof(root));if(!n||n>=sizeof(root))return 1;char*s=strrchr(root,'\\');if(s)*s=0;WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w))return 1;SOCKET server=INVALID_SOCKET;int port;for(port=8765;port<=8785;port++){server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((u_short)port);if(server!=INVALID_SOCKET&&bind(server,(struct sockaddr*)&a,sizeof(a))==0&&listen(server,SOMAXCONN)==0)break;if(server!=INVALID_SOCKET)closesocket(server);server=INVALID_SOCKET;}if(server==INVALID_SOCKET){MessageBoxA(NULL,"Ports 8765-8785 are unavailable.","GPT Image Tool",MB_ICONERROR);return 1;}char url[256];snprintf(url,sizeof(url),"http://127.0.0.1:%d/index.html",port);printf("GPT Image Tool PC Developer v1.0 started.\nParallel generation/edit proxy: enabled.\nURL: %s\nKeep this window open. Press Ctrl+C to stop.\n",url);ShellExecuteA(NULL,"open",url,NULL,root,SW_SHOWNORMAL);for(;;){SOCKET c=accept(server,NULL,NULL);if(c==INVALID_SOCKET)break;CLIENT_JOB*j=(CLIENT_JOB*)calloc(1,sizeof(CLIENT_JOB));if(!j){closesocket(c);continue;}j->client=c;strncpy(j->root,root,sizeof(j->root)-1);HANDLE th=CreateThread(NULL,0,client_worker,j,0,NULL);if(th)CloseHandle(th);else{closesocket(c);free(j);}}return 0;}
