#import <Foundation/Foundation.h>
#include "sqlite3.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#define WINEMETAL_API
#include "../winemetal_thunks.h"

@interface CacheReader : NSObject
- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version;
- (dispatch_data_t)get:(NSData *)key;
- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version status:(uint32_t *)status;
- (dispatch_data_t)get:(NSData *)key status:(uint32_t *)status;
@end

@interface CacheWriter : NSObject
- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version;
- (void)set:(NSData *)key value:(dispatch_data_t)value;
- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version status:(uint32_t *)status;
- (uint32_t)setChecked:(NSData *)key value:(dispatch_data_t)value;
@end

@interface CacheReader () {
  sqlite3 *_db;
  sqlite3_stmt *_stmt;
}
@end

static inline NSString *
resolve_cache_dir(NSString *path, bool path_is_file) {
  if (![path hasPrefix:@"/"]) {
    char buf[PATH_MAX];
    size_t len = confstr(_CS_DARWIN_USER_CACHE_DIR, buf, sizeof(buf));

    if (!len) {
      return nil;
    }

    NSString *base = [NSString stringWithUTF8String:buf];
    path = [base stringByAppendingPathComponent:path];
  }
  [[NSFileManager defaultManager] createDirectoryAtPath:path_is_file ? [path stringByDeletingLastPathComponent] : path
                            withIntermediateDirectories:YES
                                             attributes:nil
                                                  error:nil];
  return path;
}

static inline void
cache_set_status(uint32_t *status, int result) {
  if (status)
    *status = (uint32_t)result;
}

static inline int
cache_operation_status(sqlite3 *db, int result) {
  if (result == SQLITE_OK || result == SQLITE_ROW || result == SQLITE_DONE)
    return result;
  int extended = db ? sqlite3_extended_errcode(db) : result;
  return (extended & 0xff) == (result & 0xff) ? extended : result;
}

@implementation CacheReader

- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version {
  return [self initWithPath:path version:version status:NULL];
}

- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version status:(uint32_t *)status {
  cache_set_status(status, SQLITE_OK);
  if ((self = [super init])) {
    NSString *dbPath = path ? resolve_cache_dir(path, true) : nil;
    if (!dbPath) {
      cache_set_status(status, SQLITE_CANTOPEN);
      [self release];
      return nil;
    }
    NSString *tableName = [NSString stringWithFormat:@"cache_%llu", version];
    int result = sqlite3_open_v2([dbPath fileSystemRepresentation], &_db,
                               SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
    if (result != SQLITE_OK) {
      NSLog(@"[CacheReader] Failed to open DB: %s", sqlite3_errmsg(_db));
      cache_set_status(status, cache_operation_status(_db, result));
      [self release];
      return nil;
    }
    NSString *sqlGet = [NSString stringWithFormat:@"SELECT value FROM %@ WHERE key = ?;", tableName];
    result = sqlite3_prepare_v2(_db, sqlGet.UTF8String, -1, &_stmt, NULL);
    if (result != SQLITE_OK) {
      NSLog(@"[CacheReader] Failed to prepare SELECT: %s", sqlite3_errmsg(_db));
      cache_set_status(status, cache_operation_status(_db, result));
      [self release];
      return nil;
    }
  } else {
    cache_set_status(status, SQLITE_NOMEM);
  }
  return self;
}

- (dispatch_data_t)get:(NSData *)key {
  return [self get:key status:NULL];
}

- (dispatch_data_t)get:(NSData *)key status:(uint32_t *)status {
  sqlite3_reset(_stmt);
  sqlite3_clear_bindings(_stmt);
  int result = sqlite3_bind_blob64(_stmt, 1, key.bytes, key.length, SQLITE_STATIC);
  if (result == SQLITE_OK)
    result = sqlite3_step(_stmt);
  dispatch_data_t data = nil;
  if (result == SQLITE_ROW) {
    const void *bytes = sqlite3_column_blob(_stmt, 0);
    int len = sqlite3_column_bytes(_stmt, 0);
    data = dispatch_data_create(bytes, len, nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    if (!data)
      result = SQLITE_NOMEM;
  }
  cache_set_status(status, cache_operation_status(_db, result));
  // SQLITE_STATIC bindings must be cleared while the caller's key is alive.
  sqlite3_reset(_stmt);
  sqlite3_clear_bindings(_stmt);
  return data;
}

- (void)dealloc {
  if (_stmt)
    sqlite3_finalize(_stmt);
  if (_db)
    sqlite3_close(_db);
  [super dealloc];
}

@end

@interface CacheWriter () {
  sqlite3 *_db;
  sqlite3_stmt *_stmt;
}
@end

@implementation CacheWriter

- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version {
  return [self initWithPath:path version:version status:NULL];
}

- (instancetype)initWithPath:(NSString *)path version:(uint64_t)version status:(uint32_t *)status {
  cache_set_status(status, SQLITE_OK);
  if ((self = [super init])) {
    NSString *dbPath = path ? resolve_cache_dir(path, true) : nil;
    if (!dbPath) {
      cache_set_status(status, SQLITE_CANTOPEN);
      [self release];
      return nil;
    }
    NSString *lockPath = [dbPath stringByAppendingString:@"-lock"];
    int fd = open([lockPath fileSystemRepresentation], O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
      NSLog(@"[CacheWriter] Failed to open file for locking %@", lockPath);
      cache_set_status(status, SQLITE_CANTOPEN);
      [self release];
      return nil;
    }
    if (flock(fd, LOCK_EX) != 0) {
      close(fd);
      cache_set_status(status, SQLITE_BUSY);
      [self release];
      return nil;
    }
    int result = sqlite3_open_v2([dbPath fileSystemRepresentation], &_db,
                                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);
    if (result == SQLITE_OK)
      result = sqlite3_exec(_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    if (result == SQLITE_OK)
      result = sqlite3_exec(_db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    NSString *tableName = [NSString stringWithFormat:@"cache_%llu", version];
    NSString *sqlCreate = [NSString stringWithFormat:@"CREATE TABLE IF NOT EXISTS %@ ("
                                                      "key BLOB PRIMARY KEY, "
                                                      "value BLOB NOT NULL);", tableName];
    if (result == SQLITE_OK)
      result = sqlite3_exec(_db, sqlCreate.UTF8String, NULL, NULL, NULL);
    flock(fd, LOCK_UN);
    close(fd);
    if (result != SQLITE_OK) {
      NSLog(@"[CacheWriter] Failed to initialize DB: %s", sqlite3_errmsg(_db));
      cache_set_status(status, cache_operation_status(_db, result));
      [self release];
      return nil;
    }
    NSString *sqlSet = [NSString stringWithFormat:@"INSERT OR REPLACE INTO %@ (key, value) VALUES (?, ?);", tableName];
    result = sqlite3_prepare_v2(_db, sqlSet.UTF8String, -1, &_stmt, NULL);
    if (result != SQLITE_OK) {
      NSLog(@"[CacheWriter] Failed to prepare INSERT: %s", sqlite3_errmsg(_db));
      cache_set_status(status, cache_operation_status(_db, result));
      [self release];
      return nil;
    }
  } else {
    cache_set_status(status, SQLITE_NOMEM);
  }
  return self;
}

- (void)set:(NSData *)key value:(dispatch_data_t)value {
  (void)[self setChecked:key value:value];
}

- (uint32_t)setChecked:(NSData *)key value:(dispatch_data_t)value {
  sqlite3_reset(_stmt);
  sqlite3_clear_bindings(_stmt);
  int result = sqlite3_bind_blob64(_stmt, 1, key.bytes, key.length, SQLITE_STATIC);
  const void *bytes = NULL;
  size_t length = 0;
  dispatch_data_t flat = dispatch_data_create_map(value, &bytes, &length);
  if (!flat)
    result = SQLITE_NOMEM;
  if (result == SQLITE_OK)
    result = sqlite3_bind_blob64(_stmt, 2, bytes, length, SQLITE_STATIC);
  if (result == SQLITE_OK)
    result = sqlite3_step(_stmt);
  uint32_t status = (uint32_t)cache_operation_status(_db, result);
  if (result != SQLITE_DONE)
    NSLog(@"[CacheWriter] Failed to insert: %s", sqlite3_errmsg(_db));
  // Clear SQLITE_STATIC references before releasing the mapped payload/key.
  sqlite3_reset(_stmt);
  sqlite3_clear_bindings(_stmt);
  if (flat)
    dispatch_release(flat);
  return status;
}

- (void)dealloc {
  if (_stmt)
    sqlite3_finalize(_stmt);
  if (_db)
    sqlite3_close(_db);
  [super dealloc];
}

@end

int
_CacheReader_alloc_init(void *obj) {
  struct unixcall_cache_alloc_init *params = obj;
  NSString *path = [[NSString alloc] initWithCString:params->path.ptr encoding:NSUTF8StringEncoding];
  params->ret_cache = (obj_handle_t)[[CacheReader alloc] initWithPath:path version:params->version];
  [path release];
  return 0;
}

int
_CacheReader_get(void *obj) {
  struct unixcall_cache_get *params = obj;
  NSData *key =
      [[NSData alloc] initWithBytesNoCopy:(void *)params->key.ptr length:params->key_length freeWhenDone:false];
  CacheReader *reader = (CacheReader *)params->cache;
  params->ret_data = (obj_handle_t)[reader get:key];
  [key release];
  return 0;
}

int
_CacheWriter_alloc_init(void *obj) {
  struct unixcall_cache_alloc_init *params = obj;
  NSString *path = [[NSString alloc] initWithCString:params->path.ptr encoding:NSUTF8StringEncoding];
  params->ret_cache = (obj_handle_t)[[CacheWriter alloc] initWithPath:path version:params->version];
  [path release];
  return 0;
}

int
_CacheWriter_set(void *obj) {
  struct unixcall_cache_set *params = obj;
  NSData *key =
      [[NSData alloc] initWithBytesNoCopy:(void *)params->key.ptr length:params->key_length freeWhenDone:false];
  CacheWriter *writer = (CacheWriter *)params->cache;
  [writer set:key value:(dispatch_data_t)params->value_data];
  [key release];
  return 0;
}

int
_Cache_alloc_init_checked(void *obj) {
  struct unixcall_cache_alloc_init_checked *params = obj;
  params->ret_cache = 0;
  params->ret_status = SQLITE_MISUSE;
  if (!params->path.ptr || params->writer > 1)
    return 0;
  @autoreleasepool {
    NSString *path = [[NSString alloc] initWithCString:params->path.ptr encoding:NSUTF8StringEncoding];
    if (path) {
      if (params->writer)
        params->ret_cache = (obj_handle_t)[[CacheWriter alloc] initWithPath:path version:params->version
                                                                  status:&params->ret_status];
      else
        params->ret_cache = (obj_handle_t)[[CacheReader alloc] initWithPath:path version:params->version
                                                                  status:&params->ret_status];
    }
    [path release];
  }
  return 0;
}

int
_CacheReader_get_checked(void *obj) {
  struct unixcall_cache_get_checked *params = obj;
  params->ret_data = 0;
  params->ret_status = SQLITE_MISUSE;
  if (!params->cache || (!params->key.ptr && params->key_length))
    return 0;
  @autoreleasepool {
    NSData *key = [[NSData alloc] initWithBytesNoCopy:(void *)params->key.ptr
                                             length:params->key_length freeWhenDone:false];
    params->ret_data = (obj_handle_t)[(CacheReader *)params->cache get:key status:&params->ret_status];
    [key release];
  }
  return 0;
}

int
_CacheWriter_set_checked(void *obj) {
  struct unixcall_cache_set_checked *params = obj;
  params->ret_status = SQLITE_MISUSE;
  if (!params->cache || !params->value_data || (!params->key.ptr && params->key_length))
    return 0;
  @autoreleasepool {
    NSData *key = [[NSData alloc] initWithBytesNoCopy:(void *)params->key.ptr
                                             length:params->key_length freeWhenDone:false];
    params->ret_status = [(CacheWriter *)params->cache setChecked:key
                                                         value:(dispatch_data_t)params->value_data];
    [key release];
  }
  return 0;
}

#ifndef DXMT_NO_PRIVATE_API

extern void MTLSetShaderCachePath(NSString* path);
extern NSString* MTLGetShaderCachePath();

int
_WMTSetMetalShaderCachePath(void *obj) {
  struct unixcall_setmetalcachepath *params = obj;
  NSString *path = [[NSString alloc] initWithCString:params->path.ptr encoding:NSUTF8StringEncoding];
  NSString *resolved_path = resolve_cache_dir(path, false);
  MTLSetShaderCachePath(resolved_path);
  params->ret_success = [MTLGetShaderCachePath() isEqualToString:resolved_path];
  [path release];
  return 0;
}

#else

int
_WMTSetMetalShaderCachePath(void *obj) {
  struct unixcall_setmetalcachepath *params = obj;
  params->ret_success = 0;
  return 0;
}

#endif
