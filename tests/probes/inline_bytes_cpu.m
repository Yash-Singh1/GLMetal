#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STAGE_VERTEX=1, STAGE_FRAGMENT=2, STAGE_BOTH=3 };
@interface MockEncoder : NSObject {
@public
    NSUInteger byte_calls[2], buffer_calls[2], offset_calls[2];
    NSMutableDictionary<NSNumber *, NSData *> *values[2];
}
- (void)setVertexBytes:(const void *)data length:(NSUInteger)length atIndex:(NSUInteger)slot;
- (void)setFragmentBytes:(const void *)data length:(NSUInteger)length atIndex:(NSUInteger)slot;
- (void)setVertexBuffer:(id)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setFragmentBuffer:(id)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setVertexBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot;
- (void)setFragmentBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)slot;
@end
@implementation MockEncoder
- (id)init { self=[super init]; if(self)for(int i=0;i<2;++i)values[i]=[NSMutableDictionary dictionary]; return self; }
- (void)setVertexBytes:(const void *)p length:(NSUInteger)n atIndex:(NSUInteger)s { ++byte_calls[0]; values[0][@(s)]=[NSData dataWithBytes:p length:n]; }
- (void)setFragmentBytes:(const void *)p length:(NSUInteger)n atIndex:(NSUInteger)s { ++byte_calls[1]; values[1][@(s)]=[NSData dataWithBytes:p length:n]; }
- (void)setVertexBuffer:(id)b offset:(NSUInteger)o atIndex:(NSUInteger)s { (void)b;(void)o;++buffer_calls[0];[values[0] removeObjectForKey:@(s)]; }
- (void)setFragmentBuffer:(id)b offset:(NSUInteger)o atIndex:(NSUInteger)s { (void)b;(void)o;++buffer_calls[1];[values[1] removeObjectForKey:@(s)]; }
- (void)setVertexBufferOffset:(NSUInteger)o atIndex:(NSUInteger)s { (void)o;(void)s;++offset_calls[0]; }
- (void)setFragmentBufferOffset:(NSUInteger)o atIndex:(NSUInteger)s { (void)o;(void)s;++offset_calls[1]; }
@end
@protocol MTLBuffer @end
struct glm_backend_context {
    MockEncoder *encoder;
    struct encoder_cache { __unsafe_unretained id buffers[2][31]; NSUInteger offsets[2][31]; } cache;
    struct inline_byte_binding { uint64_t encoder; NSUInteger length,capacity; uint8_t *data; } inline_bytes[2][31];
    uint64_t encoder_serial;
    bool inline_bytes_enabled,inline_bytes_stats;
    uint64_t inline_bytes_calls,inline_bytes_skipped,inline_bytes_saved;
};
static int fail_allocation;
static void *test_realloc(void *p, size_t n) { if(fail_allocation){fail_allocation=0;return NULL;}return realloc(p,n); }
#define realloc test_realloc
/* PRODUCTION_FUNCTIONS */
#undef realloc

int main(void)
{
    @autoreleasepool {
        struct glm_backend_context b={0};
        MockEncoder *first=[MockEncoder new]; b.encoder=first;b.encoder_serial=1;b.inline_bytes_enabled=true;
        uint8_t data[8192]; memset(data,17,sizeof data);
        for(int i=0;i<10000;++i)enc_bytes(&b,STAGE_BOTH,data,3072,16);
        assert(first->byte_calls[0]==1 && first->byte_calls[1]==1);
        data[3000]=18;enc_bytes(&b,STAGE_FRAGMENT,data,3072,16);
        assert(first->byte_calls[1]==2 && first->byte_calls[0]==1);
        const uint8_t *before=[first->values[0][@16] bytes];assert(before[3000]==17);
        enc_bytes(&b,STAGE_VERTEX,data,3072,16);assert(first->byte_calls[0]==2);
        enc_bytes(&b,STAGE_VERTEX,data,32,16);assert(first->byte_calls[0]==3);
        enc_bytes(&b,STAGE_VERTEX,data,3072,16);assert(first->byte_calls[0]==4);
        /* Buffer binding, including NULL, must displace the inline bytes. */
        enc_buffer(&b,STAGE_VERTEX,nil,0,16);assert(first->buffer_calls[0]==1 && !first->values[0][@16]);
        enc_bytes(&b,STAGE_VERTEX,data,3072,16);assert(first->byte_calls[0]==5);
        NSObject *buffer=[NSObject new];
        enc_buffer(&b,STAGE_FRAGMENT,(id<MTLBuffer>)buffer,0,16);assert(first->buffer_calls[1]==1);
        enc_buffer(&b,STAGE_FRAGMENT,(id<MTLBuffer>)buffer,4,16);assert(first->offset_calls[1]==1);
        enc_bytes(&b,STAGE_FRAGMENT,data,3072,16);assert(first->byte_calls[1]==3);
        /* A new encoder needs a fresh binding even for identical bytes. */
        MockEncoder *next=[MockEncoder new];b.encoder=next;++b.encoder_serial;memset(&b.cache,0,sizeof b.cache);
        enc_bytes(&b,STAGE_BOTH,data,3072,16);assert(next->byte_calls[0]==1 && next->byte_calls[1]==1);
        /* Disabled, oversized and failed cache allocations retain original calls. */
        b.inline_bytes_enabled=false;
        enc_bytes(&b,STAGE_VERTEX,data,3072,16);enc_bytes(&b,STAGE_VERTEX,data,3072,16);
        assert(next->byte_calls[0]==3);b.inline_bytes_enabled=true;
        enc_bytes(&b,STAGE_VERTEX,data,8192,16);enc_bytes(&b,STAGE_VERTEX,data,8192,16);
        assert(next->byte_calls[0]==5);
        fail_allocation=1;enc_bytes(&b,STAGE_VERTEX,data,4096,15);
        assert(!b.inline_bytes[0][15].data && next->byte_calls[0]==6);
        enc_bytes(&b,STAGE_VERTEX,data,4096,15);enc_bytes(&b,STAGE_VERTEX,data,4096,15);
        assert(next->byte_calls[0]==7);
        enc_bytes(&b,STAGE_FRAGMENT,data,0,15);enc_bytes(&b,STAGE_FRAGMENT,data,0,15);
        assert(next->byte_calls[1]==2);
        for(int stage=0;stage<2;++stage)for(int slot=0;slot<31;++slot)free(b.inline_bytes[stage][slot].data);
        puts("inline bytes: repeated3072byte uniforms, mutations, stage independence, source lifetime, sizes, buffer/null rebinding, encoder restart, disabled/oversized/OOM retries passed");
    }
}
