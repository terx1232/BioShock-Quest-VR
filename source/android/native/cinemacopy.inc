#include "cinemaquality.inc"
GLint copyQuality=0;
int64_t copyColorFormat=GL_RGBA8;
bool copySrgbControl=false;
struct SrgbWriteGuard {
    GLboolean enabled=GL_TRUE;
    SrgbWriteGuard(){if(copySrgbControl){enabled=glIsEnabled(0x8db9);glEnable(0x8db9);}}
    ~SrgbWriteGuard(){if(copySrgbControl&&!enabled)glDisable(0x8db9);}
};
GLuint shader(GLenum type,const char* source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint good;glGetShaderiv(s,GL_COMPILE_STATUS,&good);if(!good){char log[512];glGetShaderInfoLog(s,sizeof(log),nullptr,log);ERROR("shader: %s",log);glDeleteShader(s);return 0;}return s;}
void copyImage(GLuint texture,int width,int height,bool decode,int sourceWidth=0,int sourceHeight=0,int quality=0,bool fullSource=false){
    SrgbWriteGuard encoding;
    if(!sourceWidth)sourceWidth=width;if(!sourceHeight)sourceHeight=height;
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    int viewWidth=width,viewHeight=height;
    if(fullSource){
        const GLfloat black[4]={0,0,0,1};glClearBufferfv(GL_COLOR,0,black);
        if(int64_t(sourceWidth)*height>int64_t(width)*sourceHeight)viewHeight=int(int64_t(width)*sourceHeight/sourceWidth);
        else viewWidth=int(int64_t(height)*sourceWidth/sourceHeight);
        if(viewWidth<1)viewWidth=1;if(viewHeight<1)viewHeight=1;
    }
    glViewport((width-viewWidth)/2,(height-viewHeight)/2,viewWidth,viewHeight);glUseProgram(copyProgram);glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D,texture);
    glUniform2f(copyScale,fullSource?1.f:(float)width/sourceWidth,fullSource?1.f:(float)height/sourceHeight);
    glUniform4f(copyBounds,.5f/sourceWidth,.5f/sourceHeight,((fullSource?sourceWidth:width)-.5f)/sourceWidth,((fullSource?sourceHeight:height)-.5f)/sourceHeight);
    glUniform1i(copyDecode,decode?1:0);glUniform1i(copyQuality,quality);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}
bool initCopy(){
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nconst vec2 p[4]=vec2[4](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));out vec2 uv;void main(){vec2 v=p[gl_VertexID];uv=vec2(v.x*.5+.5,.5-v.y*.5);gl_Position=vec4(v,0,1);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,kCopyFragment);
    if(!vs||!fs)return false;
    copyProgram=glCreateProgram();glAttachShader(copyProgram,vs);glAttachShader(copyProgram,fs);glLinkProgram(copyProgram);glDeleteShader(vs);glDeleteShader(fs);
    GLint good;glGetProgramiv(copyProgram,GL_LINK_STATUS,&good);if(!good)return false;
    glUseProgram(copyProgram);glUniform1i(glGetUniformLocation(copyProgram,"source"),0);copyDecode=glGetUniformLocation(copyProgram,"decodeSRGB");
    copyScale=glGetUniformLocation(copyProgram,"sourceScale");copyBounds=glGetUniformLocation(copyProgram,"sourceBounds");
    copyQuality=glGetUniformLocation(copyProgram,"quality");
    const char* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    copySrgbControl=extensions&&strstr(extensions,"GL_EXT_sRGB_write_control");
    // Test real GLES output, including row order and both source color formats.
    const unsigned char pixels[]={0,0,0,255,128,128,128,255,255,0,0,255,255,255,255,255};
    const unsigned char expected[]={255,0,0,255,255,255,255,255,0,0,0,255,55,55,55,255};
    GLuint textures[2];glGenTextures(2,textures);glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER,menuFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
    bool passed=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    for(GLint format:{GL_RGBA8,GL_SRGB8_ALPHA8}){
        glBindTexture(GL_TEXTURE_2D,textures[0]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,format,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        copyImage(textures[0],2,2,format==GL_RGBA8);unsigned char actual[16];glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        for(int i=0;i<16;++i)if(abs((int)actual[i]-expected[i])>2)passed=false;
        // An independent menu must never sample stale gameplay outside its
        // top-left rectangle, or invert around the full transport height.
        unsigned char padded[64];for(int i=0;i<64;i+=4){padded[i]=255;padded[i+1]=0;padded[i+2]=255;padded[i+3]=255;}
        memcpy(padded,pixels,8);memcpy(padded+16,pixels+8,8);
        glTexImage2D(GL_TEXTURE_2D,0,format,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,padded);
        copyImage(textures[0],2,2,format==GL_RGBA8,4,4);glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        for(int i=0;i<16;++i)if(abs((int)actual[i]-expected[i])>2)passed=false;
    }
    if(copyColorFormat==GL_SRGB8_ALPHA8){
        unsigned char ramp[1024],actual[1024];
        for(int x=0;x<256;x++){for(int c=0;c<3;c++)ramp[x*4+c]=x;ramp[x*4+3]=255;}
        glBindTexture(GL_TEXTURE_2D,textures[0]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,ramp);
        glBindTexture(GL_TEXTURE_2D,textures[1]);glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        GLboolean dither=glIsEnabled(GL_DITHER);glDisable(GL_DITHER);
        copyImage(textures[0],256,1,true);glReadPixels(0,0,256,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        unsigned seen[256]={},dark=0;
        for(int x=0;x<256;x++){if(abs(int(actual[x*4])-x)>1)passed=false;if(x<64)seen[actual[x*4]]=1;}
        for(unsigned v:seen)dark+=v;
        if(dark<63)passed=false;
        if(dither)glEnable(GL_DITHER);
        INFO("shadow precision self-test: %u of 64 near-black levels retained in sRGB output",dark);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);glDeleteTextures(2,textures);
    if(passed)INFO("GPU color test passed: RGBA/sRGB input, gray 128 to linear 55, row flip, cropped menu");
    else ERROR("GPU color test failed; using fallback");
    return passed;
}
