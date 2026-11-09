#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>

/* Cedar KV: single-process, single-client-at-a-time, loopback-only storage. */
#define BUCKETS 4093
#define LIMIT 4096
#define MAX_KEYS 100000
struct entry { char *key, *value; struct entry *next; };
static struct entry *table[BUCKETS];
static size_t count;
static FILE *journal;
static uint32_t hash(const char *s) {
    uint32_t h=2166136261u;
    for (;*s;s++) { h^=(unsigned char)*s; h*=16777619u; }
    return h;
}
static struct entry **slot(const char *key) {
    struct entry **p=&table[hash(key)%BUCKETS];
    while (*p && strcmp((*p)->key,key)) p=&(*p)->next;
    return p;
}
static void cleanup(void) {
    for (size_t i=0;i<BUCKETS;i++) {
        struct entry *e=table[i];
        while(e) { struct entry *n=e->next; free(e->key);free(e->value);free(e);e=n; }
    }
    if(journal) fclose(journal);
}
static void fatal(const char *s) { perror(s); exit(EXIT_FAILURE); }
/* Return 0 on valid command. Allocate everything BEFORE committing to disk. */
static int mutate(char *line, int persist) {
    char *space=strchr(line,' ');
    if(!space) return -1;
    *space++=0;
    int set=!strcmp(line,"SET"), del=!strcmp(line,"DELETE");
    if(!set && !del) return -1;
    char *key=space,*value=NULL;
    if(set) { value=strchr(key,' '); if(!value)return -1; *value++=0; }
    if(!*key || strlen(key)>128 || strchr(key,' ') || strchr(key,'\t')) return -1;
    struct entry **p=slot(key), *fresh=NULL;
    char *copy=NULL;
    if(set) {
        if(!*p && count>=MAX_KEYS) return -2;
        copy=strdup(value); if(!copy) fatal("allocate value");
        if(!*p) {
            fresh=calloc(1,sizeof *fresh); if(!fresh)fatal("allocate entry");
            fresh->key=strdup(key); if(!fresh->key)fatal("allocate key");
        }
    }
    if(persist) {
        int rc=set?fprintf(journal,"SET %s %s\n",key,value):fprintf(journal,"DELETE %s\n",key);
        /* No success response until flush and fsync succeed. Fail closed on errors. */
        if(rc<0 || fflush(journal) || fsync(fileno(journal)))fatal("journal commit");
    }
    if(set) {
        if(fresh) { *p=fresh;count++; }
        free((*p)->value);(*p)->value=copy;
    } else if(*p) {
        struct entry *old=*p;*p=old->next;
        free(old->key);free(old->value);free(old);count--;
    }
    return 0;
}
static void recover(const char *path) {
    int fd=open(path,O_RDWR|O_CREAT,0600);if(fd<0)fatal("open journal");
    if(flock(fd,LOCK_EX|LOCK_NB))fatal("journal already locked");
    journal=fdopen(fd,"r+");if(!journal)fatal("fdopen");
    char line[LIMIT];off_t good=0;
    while(fgets(line,sizeof line,journal)) {
        size_t n=strlen(line);
        if(!n || line[n-1]!='\n') {
            if(!feof(journal)) { fprintf(stderr,"Oversized/corrupt journal record\n");exit(1); }
            if(ftruncate(fd,good))fatal("truncate incomplete tail");
            break;
        }
        line[n-1]=0;
        if(mutate(line,0)) { fprintf(stderr,"Invalid complete journal record; refusing startup\n");exit(1); }
        good=ftello(journal);
    }
    if(ferror(journal))fatal("read journal");
    clearerr(journal);if(fseeko(journal,0,SEEK_END))fatal("seek journal");
}
static void serve(int fd) {
    FILE *io=fdopen(fd,"r+");if(!io) { close(fd);return; }
    setvbuf(io,NULL,_IONBF,0);
    char line[LIMIT];size_t used=0;int overflow=0,invalid=0;
    /* byte framing handles TCP fragmentation and several commands per packet */
    for(;;) {
        unsigned char c;ssize_t n=recv(fd,&c,1,0);
        if(n<=0)break;
        if(c!='\n') {
            if(c<32 || c>126)invalid=1;
            if(used<sizeof(line)-1)line[used++]=(char)c;else overflow=1;
            continue;
        }
        line[used]=0;
        if(overflow || invalid)fprintf(io,"ERR invalid or oversized command\n");
        else if(!strcmp(line,"PING"))fprintf(io,"PONG\n");
        else if(!strcmp(line,"STATS"))fprintf(io,"KEYS %zu\n",count);
        else if(!strncmp(line,"GET ",4) && line[4] && !strchr(line+4,' ')) {
            struct entry *e=*slot(line+4);
            if(e)fprintf(io,"VALUE %s\n",e->value);else fprintf(io,"NOT_FOUND\n");
        } else {
            int rc=mutate(line,1);
            fprintf(io,rc==0?"OK\n":rc==-2?"ERR key limit\n":"ERR command\n");
        }
        used=0;overflow=0;invalid=0;
        if(ferror(io))break;
    }
    fclose(io);
}
int main(int argc,char **argv) {
    if(argc!=3) { fprintf(stderr,"Usage: %s PORT JOURNAL\n",argv[0]);return 2; }
    char *end;errno=0;long port=strtol(argv[1],&end,10);
    if(errno || *end || port<1 || port>65535)return 2;
    signal(SIGPIPE,SIG_IGN);atexit(cleanup);recover(argv[2]);
    int sock=socket(AF_INET,SOCK_STREAM,0);if(sock<0)fatal("socket");
    int yes=1;setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(sock,(struct sockaddr*)&a,sizeof a) || listen(sock,16))fatal("listen");
    printf("READY 127.0.0.1:%ld keys=%zu\n",port,count);fflush(stdout);
    for(;;) { int fd=accept(sock,NULL,NULL);if(fd<0) { if(errno==EINTR)continue;fatal("accept"); } serve(fd); }
}
