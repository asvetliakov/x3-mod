// Original host-only GL -> IOSurface -> Metal FP16 proof. No window or Wine.
#define GL_SILENCE_DEPRECATION
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/CGLIOSurface.h>
#import <OpenGL/gl3.h>
#include <cmath>
#include <cstdio>
int main(){@autoreleasepool {
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf=nullptr;GLint count=0;CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    CGLContextObj gl=nullptr;if(e==kCGLNoError)e=CGLCreateContext(pf,nullptr,&gl);if(pf)CGLDestroyPixelFormat(pf);
    std::printf("CGL create=%d\n",int(e));if(e!=kCGLNoError||!gl)return 2;CGLSetCurrentContext(gl);
    std::printf("GL renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    NSDictionary* properties=@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@8,(id)kIOSurfaceBytesPerRow:@512,(id)kIOSurfaceAllocSize:@32768,(id)kIOSurfacePixelFormat:@0x52476841}; // 'RGhA'
    IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)properties);if(!surface)return 3;
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE,texture);
    e=CGLTexImageIOSurface2D(gl,GL_TEXTURE_RECTANGLE,GL_RGBA16F,64,64,GL_RGBA,GL_HALF_FLOAT,surface,0);
    std::printf("CGL IOSurface RGBA16F bind=%d\n",int(e));if(e!=kCGLNoError)return 4;
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_RECTANGLE,texture,0);
    const GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);std::printf("GL framebuffer=%x\n",status);if(status!=GL_FRAMEBUFFER_COMPLETE)return 5;
    const GLfloat hdr[]={.18f,2,4,1};glClearBufferfv(GL_COLOR,0,hdr);glFinish();const GLenum error=glGetError();std::printf("GL producer_error=%x sync=glFinish\n",error);if(error)return 6;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();MTLTextureDescriptor* desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:64 height:64 mipmapped:NO];desc.usage=MTLTextureUsageShaderRead;desc.storageMode=MTLStorageModeShared;
    id<MTLTexture> metal=[device newTextureWithDescriptor:desc iosurface:surface plane:0];std::printf("Metal device=%s IOSurface_texture=%u\n",device.name.UTF8String,metal!=nil);if(!metal)return 7;
    NSError* err=nil;NSString* source=@"#include <metal_stdlib>\nusing namespace metal;kernel void verify(texture2d<half, access::read> t [[texture(0)]], device float4* out [[buffer(0)]]){out[0]=float4(t.read(uint2(0,0)));}";
    id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&err];if(!library){std::printf("Metal compiler=%s\n",err.description.UTF8String);return 8;}
    id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"verify"] error:&err];if(!pipeline)return 9;
    id<MTLBuffer> output=[device newBufferWithLength:16 options:MTLResourceStorageModeShared];id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];[encoder setTexture:metal atIndex:0];[encoder setBuffer:output offset:0 atIndex:0];[encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];[encoder endEncoding];[command commit];[command waitUntilCompleted];
    const float* values=(const float*)output.contents;const bool pass=command.status==MTLCommandBufferStatusCompleted&&std::fabs(values[0]-.18f)<.001f&&values[1]==2&&values[2]==4&&values[3]==1;
    std::printf("Metal validation_pixel=%g,%g,%g,%g command_status=%lu RESULT=%s\n",values[0],values[1],values[2],values[3],(unsigned long)command.status,pass?"PASS":"FAIL");
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);CFRelease(surface);CGLSetCurrentContext(nullptr);CGLDestroyContext(gl);
    return pass?0:10;
}}
