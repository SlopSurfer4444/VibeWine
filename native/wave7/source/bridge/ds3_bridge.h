// Local candidate compatibility branch; no PE ABI or Unix-call table changes.
#include <CommonCrypto/CommonDigest.h>
#include <crt_externs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "expected.h"
#include "patched_source.h"
static int ds3_digest_matches(const void *data, size_t len, const char *expected) {
 unsigned char digest[CC_SHA256_DIGEST_LENGTH];char hex[65];CC_SHA256_CTX ctx;
 CC_SHA256_Init(&ctx);
 const unsigned char *p=data;
 while(len){CC_LONG n=len>0x10000000?0x10000000:(CC_LONG)len;CC_SHA256_Update(&ctx,p,n);p+=n;len-=n;}
 CC_SHA256_Final(digest,&ctx);
 for(unsigned i=0;i<32;i++)sprintf(hex+i*2,"%02x",digest[i]);hex[64]=0;
 return strcmp(hex,expected)==0;
}
static int ds3_process_gate(void) {
 const char *opt=getenv("DXMT_DS3_MARSHAL_FIX");if(!opt||strcmp(opt,"1"))return 0;
 int argc=*_NSGetArgc();char **argv=*_NSGetArgv();
 for(int i=0;i<argc&&i<2;i++){
  const char *base=argv[i];for(const char *p=base;*p;p++)if(*p=='/'||*p=='\\')base=p+1;
  if(!strcasecmp(base,"DarkSoulsIII.exe"))return 1;
 }
 return 0;
}
static void *ds3_airconv_symbol(const char *name) {
 static void *handle;static dispatch_once_t once;
 dispatch_once(&once, ^{
  Dl_info info;
  if(!dladdr((void *)&ds3_airconv_symbol,&info)||!info.dli_fname)abort();
  NSString *path=[[@(info.dli_fname) stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"winemetal-airconv-original.so"];
  NSData *data=[NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:NULL];
  if(!data||!ds3_digest_matches(data.bytes,data.length,DS3_DONOR_SHA)){fprintf(stderr,"DS3 bridge donor integrity failure\n");abort();}
  handle=dlopen(path.fileSystemRepresentation,RTLD_NOW|RTLD_LOCAL);
  if(!handle){fprintf(stderr,"DS3 bridge donor load failed\n");abort();}
 });
 void *fn=dlsym(handle,name);if(!fn){fprintf(stderr,"DS3 bridge donor symbol missing\n");abort();}return fn;
}
static id<MTLLibrary> ds3_new_library(id<MTLDevice> device,dispatch_data_t data,NSError **error) {
 const char *enabled=getenv("DXMT_DS3_MARSHAL_FIX");
 if(enabled&&!strcmp(enabled,"1")&&dispatch_data_get_size(data)==DS3_HELPER_LEN){
  const void *bytes=NULL;size_t len=0;dispatch_data_t mapped=dispatch_data_create_map(data,&bytes,&len);
  if(!mapped||!bytes){if(error)*error=[NSError errorWithDomain:@"DS3Bridge" code:1 userInfo:nil];return nil;}
  int match=ds3_digest_matches(bytes,len,DS3_HELPER_SHA);dispatch_release(mapped);
  if(match){
   if(!ds3_process_gate()){fprintf(stderr,"DS3 bridge executable identity rejected; helper blocked\n");if(error)*error=[NSError errorWithDomain:@"DS3Bridge" code:2 userInfo:nil];return nil;}
   NSString *source=[[NSString alloc]initWithBytes:ds3_source length:sizeof(ds3_source)-1 encoding:NSUTF8StringEncoding];
   MTLCompileOptions *options=[[MTLCompileOptions alloc]init];options.languageVersion=MTLLanguageVersion3_1;
   id<MTLLibrary>lib=[device newLibraryWithSource:source options:options error:error];
   [source release];[options release];
   fprintf(stderr,lib?"DS3 marshal guarded helper compiled\n":"DS3 marshal guarded helper compile FAILED\n");
   return lib; // Fail closed: never retry known-defective helper after compile failure.
  }
 }
 return [device newLibraryWithData:data error:error];
}
