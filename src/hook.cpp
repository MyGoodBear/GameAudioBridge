#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <cmath>
#include <tlhelp32.h>
#include <objbase.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Experimental opt-in per-process prototype. Converts supported WASAPI PCM/float streams to OpenAL formats.
// Unsupported formats are passed through unchanged; it never intentionally silences them.

namespace {
using ALboolean = char; using ALchar = char; using ALint = int; using ALuint = unsigned int;
using ALsizei = int; using ALenum = int; using ALfloat = float; using ALvoid = void;
struct ALCdevice; struct ALCcontext;
constexpr ALenum AL_FORMAT_MONO16 = 0x1101, AL_FORMAT_STEREO16 = 0x1103;
constexpr ALenum AL_FORMAT_MONO_FLOAT32 = 0x10010, AL_FORMAT_STEREO_FLOAT32 = 0x10011;
constexpr ALenum AL_FORMAT_QUAD32 = 0x1206, AL_FORMAT_51CHN32 = 0x120C, AL_FORMAT_61CHN32 = 0x120F, AL_FORMAT_71CHN32 = 0x1212;
constexpr ALenum AL_EXT_MCFORMATS = 0x1200;
constexpr ALenum ALC_DEVICE_SPECIFIER = 0x1005;
constexpr ALenum AL_VENDOR = 0xB001, AL_VERSION = 0xB002, AL_RENDERER = 0xB003;
constexpr ALenum AL_BUFFERS_PROCESSED = 0x1016, AL_SOURCE_STATE = 0x1010, AL_PLAYING = 0x1012;
using LPALCOPENDEVICE = ALCdevice* (__cdecl*)(const ALchar*);
using LPALCCREATECONTEXT = ALCcontext* (__cdecl*)(ALCdevice*, const ALint*);
using LPALCMAKECONTEXTCURRENT = ALboolean (__cdecl*)(ALCcontext*);
using LPALCDESTROYCONTEXT = void (__cdecl*)(ALCcontext*);
using LPALCCLOSEDEVICE = ALboolean (__cdecl*)(ALCdevice*);
using LPALGENSOURCES = void (__cdecl*)(ALsizei, ALuint*); using LPALDELETESOURCES = void (__cdecl*)(ALsizei, const ALuint*);
using LPALSOURCEI = void (__cdecl*)(ALuint, ALenum, ALint); using LPALSOURCEPLAY = void (__cdecl*)(ALuint);
using LPALSOURCESTOP = void (__cdecl*)(ALuint); using LPALSOURCEQUEUEBUFFERS = void (__cdecl*)(ALuint, ALsizei, const ALuint*);
using LPALSOURCEUNQUEUEBUFFERS = void (__cdecl*)(ALuint, ALsizei, ALuint*); using LPALGENBUFFERS = void (__cdecl*)(ALsizei, ALuint*);
using LPALDELETEBUFFERS = void (__cdecl*)(ALsizei, const ALuint*); using LPALBUFFERDATA = void (__cdecl*)(ALuint, ALenum, const ALvoid*, ALsizei, ALsizei);
using LPALGETSOURCEI = void (__cdecl*)(ALuint, ALenum, ALint*); using LPALGETERROR = ALenum (__cdecl*)(); using LPALISEXTENSIONPRESENT = ALboolean (__cdecl*)(const ALchar*);
using LPALCGETSTRING = const ALchar* (__cdecl*)(ALCdevice*, ALenum);
using LPALGETSTRING = const ALchar* (__cdecl*)(ALenum);
struct ALApi {
 HMODULE dll{}; LPALCOPENDEVICE alcOpenDevice{}; LPALCCREATECONTEXT alcCreateContext{}; LPALCMAKECONTEXTCURRENT alcMakeContextCurrent{};
 LPALCDESTROYCONTEXT alcDestroyContext{}; LPALCCLOSEDEVICE alcCloseDevice{}; LPALGENSOURCES alGenSources{}; LPALDELETESOURCES alDeleteSources{};
 LPALSOURCEI alSourcei{}; LPALSOURCEPLAY alSourcePlay{}; LPALSOURCESTOP alSourceStop{}; LPALSOURCEQUEUEBUFFERS alSourceQueueBuffers{};
 LPALSOURCEUNQUEUEBUFFERS alSourceUnqueueBuffers{}; LPALGENBUFFERS alGenBuffers{}; LPALDELETEBUFFERS alDeleteBuffers{}; LPALBUFFERDATA alBufferData{};
 LPALGETSOURCEI alGetSourcei{}; LPALGETERROR alGetError{}; LPALISEXTENSIONPRESENT alIsExtensionPresent{}; LPALCGETSTRING alcGetString{}; LPALGETSTRING alGetString{};
} al;

std::mutex logMutex;
void Log(const char* msg) {
 char tmp[MAX_PATH]{}; DWORD n = GetTempPathA(MAX_PATH, tmp); if (!n || n >= MAX_PATH) return;
 std::string p(tmp); p += "GameAudioBridge.log";
 std::lock_guard<std::mutex> g(logMutex); FILE* f = nullptr; fopen_s(&f, p.c_str(), "ab"); if (f) { SYSTEMTIME st{}; GetLocalTime(&st); fprintf(f, "%02u:%02u:%02u.%03u [pid %lu] %s\r\n", st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,GetCurrentProcessId(),msg); fclose(f); }
}
struct Chunk { std::vector<uint8_t> bytes; };
std::mutex qMutex; std::deque<Chunk> queue; constexpr size_t MAX_QUEUE = 12;
std::atomic<bool> outputReady{false}, workerStop{false}; std::thread worker;
std::mutex setupMutex; bool setupAttempted=false; unsigned channels=0, inputChannels=0, sampleRate=0, inputBits=0, inputValidBits=0; DWORD channelMask=0; bool inputFloat=false, inputExtensible=false, outputFloat=false; ALenum alFormat=0;
std::mutex stateMutex; std::unordered_map<void*, bool> clientBridge; std::unordered_set<void*> renderBridge;
ALCdevice* device=nullptr; ALCcontext* context=nullptr; ALuint source=0; ALuint buffers[12]{}; bool alReady=false; std::atomic<bool> systemOpenALLoaded{false}; std::atomic<bool> openALDeviceProbeOk{false}; std::atomic<bool> creativeOpenALConfirmed{false}; std::atomic<bool> openALImportsRedirected{false}; std::atomic<bool> openALImportIncompatibleLogged{false};

bool LoadAL() {
 if (systemOpenALLoaded.load(std::memory_order_acquire)) return true;
 // Load the system OpenAL runtime explicitly. This avoids accidentally loading a game-local
 // OpenAL Soft copy from the game's current directory when hardware Creative OpenAL is installed.
 wchar_t sysDir[MAX_PATH]{}; UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
 if (!sysLen || sysLen >= MAX_PATH) { Log("GetSystemDirectory failed; cannot select system OpenAL runtime."); return false; }
 std::wstring openalPath(sysDir); openalPath += L"\\OpenAL32.dll";
 al.dll = LoadLibraryW(openalPath.c_str());
 if (!al.dll) { char msg[160]{}; sprintf_s(msg, "System OpenAL32.dll could not be loaded (Win32 error %lu); bridge remains pass-through.", GetLastError()); Log(msg); return false; }
 wchar_t loadedPath[MAX_PATH]{}; GetModuleFileNameW(al.dll, loadedPath, MAX_PATH);
 char loadedMsg[MAX_PATH * 2]{}; WideCharToMultiByte(CP_UTF8, 0, loadedPath, -1, loadedMsg, sizeof(loadedMsg), nullptr, nullptr);
 Log((std::string("Loaded system OpenAL runtime: ") + loadedMsg).c_str());
#define GETAL(field, name) al.field = reinterpret_cast<decltype(al.field)>(GetProcAddress(al.dll, name)); if (!al.field) { Log("Missing OpenAL export; bridge remains pass-through."); return false; }
 GETAL(alcOpenDevice,"alcOpenDevice"); GETAL(alcCreateContext,"alcCreateContext"); GETAL(alcMakeContextCurrent,"alcMakeContextCurrent");
 GETAL(alcDestroyContext,"alcDestroyContext"); GETAL(alcCloseDevice,"alcCloseDevice"); GETAL(alGenSources,"alGenSources"); GETAL(alDeleteSources,"alDeleteSources");
 GETAL(alSourcei,"alSourcei"); GETAL(alSourcePlay,"alSourcePlay"); GETAL(alSourceStop,"alSourceStop"); GETAL(alSourceQueueBuffers,"alSourceQueueBuffers");
 GETAL(alSourceUnqueueBuffers,"alSourceUnqueueBuffers"); GETAL(alGenBuffers,"alGenBuffers"); GETAL(alDeleteBuffers,"alDeleteBuffers"); GETAL(alBufferData,"alBufferData");
 GETAL(alGetSourcei,"alGetSourcei"); GETAL(alGetError,"alGetError"); GETAL(alIsExtensionPresent,"alIsExtensionPresent"); GETAL(alcGetString,"alcGetString"); GETAL(alGetString,"alGetString");
#undef GETAL
 systemOpenALLoaded.store(true, std::memory_order_release);
 return true;
}
bool LooksLikeCreative(const std::string& vendor, const std::string& renderer, const std::string& device) {
 auto has=[](std::string s, const char* needle) { std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); std::string n(needle); std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return s.find(n)!=std::string::npos; };
 (void)device;
 return has(vendor,"creative") || has(renderer,"creative");
}
void ProbeOpenALDevice() {
 if (!LoadAL()) return;
 ALCdevice* probeDevice = al.alcOpenDevice(nullptr);
 if (!probeDevice) { Log("OpenAL system runtime loaded, but alcOpenDevice(default) failed; native FMOD OpenAL will not be forced."); return; }
 ALCcontext* probeContext = al.alcCreateContext(probeDevice, nullptr);
 if (!probeContext) { Log("OpenAL default device opened, but context creation failed; native FMOD OpenAL will not be forced."); al.alcCloseDevice(probeDevice); return; }
 if (al.alcMakeContextCurrent(probeContext)) {
   auto safeAL = [](const ALchar* p) -> std::string { return p ? std::string(p) : std::string("(unknown)"); };
   std::string vendor = safeAL(al.alGetString(AL_VENDOR));
   std::string renderer = safeAL(al.alGetString(AL_RENDERER));
   std::string version = safeAL(al.alGetString(AL_VERSION));
   std::string devName = safeAL(al.alcGetString(probeDevice, ALC_DEVICE_SPECIFIER));
   Log((std::string("OpenAL probe: vendor=") + vendor + "; renderer=" + renderer + "; version=" + version + "; device=" + devName).c_str());
   bool creative = LooksLikeCreative(vendor, renderer, devName);
   creativeOpenALConfirmed.store(creative, std::memory_order_release);
   openALDeviceProbeOk.store(creative, std::memory_order_release);
   if (!creative) Log("Creative OpenAL mode unavailable: detected OpenAL identity does not identify Creative; bridge will not force OpenAL redirection.");
   else Log("Creative OpenAL implementation/device identified. This does not independently prove hardware acceleration for every path.");
   al.alcMakeContextCurrent(nullptr);
 } else Log("OpenAL probe context could not be made current; native FMOD OpenAL will not be forced.");
 al.alcDestroyContext(probeContext);
 al.alcCloseDevice(probeDevice);
}
void OutputThread() {
 if (!alReady || !al.alcMakeContextCurrent(context)) { Log("OpenAL context could not be made current on output thread."); outputReady=false; return; }
 al.alGenSources(1,&source); al.alGenBuffers(12,buffers);
 std::deque<ALuint> freeBuffers; for (auto b:buffers) freeBuffers.push_back(b);
 outputReady.store(source != 0, std::memory_order_release);
 if (!source) { Log("OpenAL source creation failed."); return; }
 while (!workerStop.load()) {
   ALint processed=0; al.alGetSourcei(source,AL_BUFFERS_PROCESSED,&processed);
   while (processed-- > 0) { ALuint b=0; al.alSourceUnqueueBuffers(source,1,&b); freeBuffers.push_back(b); }
   Chunk c; bool have=false;
   { std::lock_guard<std::mutex> g(qMutex); if (!queue.empty() && !freeBuffers.empty()) { c=std::move(queue.front()); queue.pop_front(); have=true; } }
   if (have) {
     ALuint b=freeBuffers.front(); freeBuffers.pop_front();
     if (c.bytes.size() <= static_cast<size_t>(INT_MAX)) {
       al.alBufferData(b,alFormat,c.bytes.data(),static_cast<ALsizei>(c.bytes.size()),static_cast<ALsizei>(sampleRate));
       if (al.alGetError()==0) { al.alSourceQueueBuffers(source,1,&b); ALint state=0; al.alGetSourcei(source,AL_SOURCE_STATE,&state); if (state!=AL_PLAYING) al.alSourcePlay(source); }
       else freeBuffers.push_back(b);
     } else freeBuffers.push_back(b);
   } else Sleep(2);
 }
 al.alSourceStop(source); ALint processed=0; al.alGetSourcei(source,AL_BUFFERS_PROCESSED,&processed);
 while (processed-- > 0) { ALuint b=0; al.alSourceUnqueueBuffers(source,1,&b); }
 al.alDeleteSources(1,&source); al.alDeleteBuffers(12,buffers); al.alcMakeContextCurrent(nullptr);
}

bool GetInputFormat(const WAVEFORMATEX* wf, unsigned& ch, unsigned& bits, unsigned& validBits, bool& isFloat, DWORD& mask, bool& extensible) {
 if (!wf || wf->nChannels < 1 || wf->nChannels > 8 || wf->nSamplesPerSec < 8000 || wf->nSamplesPerSec > 192000) return false;
 ch=wf->nChannels; bits=wf->wBitsPerSample; validBits=bits; isFloat=false; mask=0; extensible=false;
 if (wf->wFormatTag==WAVE_FORMAT_IEEE_FLOAT) isFloat=true;
 else if (wf->wFormatTag==WAVE_FORMAT_PCM) isFloat=false;
 else if (wf->wFormatTag==WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
   auto ex=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf); extensible=true; mask=ex->dwChannelMask; validBits=ex->Samples.wValidBitsPerSample;
   if (IsEqualGUID(ex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) isFloat=true;
   else if (IsEqualGUID(ex->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) isFloat=false;
   else return false;
 } else return false;
 if (isFloat && bits!=32 && bits!=64) return false;
 if (!isFloat && bits!=8 && bits!=16 && bits!=24 && bits!=32) return false;
 if (!validBits || validBits>bits) return false;
 // Multichannel requires a declared, recognized channel layout; otherwise order is ambiguous.
 if (ch>2 && (!extensible || !mask)) return false;
 return true;
}

bool SetupOpenAL(const WAVEFORMATEX* wf) {
 std::lock_guard<std::mutex> g(setupMutex);
 unsigned inCh=0,bits=0,valid=0; bool isFloat=false,ext=false; DWORD mask=0;
 if (!GetInputFormat(wf,inCh,bits,valid,isFloat,mask,ext)) { Log("Unsupported/ambiguous WASAPI format; original output retained."); return false; }
 if (setupAttempted) return outputReady.load() && inputChannels==inCh && sampleRate==wf->nSamplesPerSec && inputBits==bits && inputFloat==isFloat && channelMask==mask;
 setupAttempted=true; inputChannels=inCh; inputBits=bits; inputValidBits=valid; inputFloat=isFloat; inputExtensible=ext; channelMask=mask; sampleRate=wf->nSamplesPerSec;
 if (!LoadAL()) return false;
 device=al.alcOpenDevice(nullptr); if (!device) { Log("OpenAL could not open default device; original output retained."); return false; }
 context=al.alcCreateContext(device,nullptr); if (!context) { Log("OpenAL context creation failed; original output retained."); al.alcCloseDevice(device); device=nullptr; return false; }
 al.alcMakeContextCurrent(context);
 auto safeAL = [](const ALchar* p) -> std::string { return p ? std::string(p) : std::string("(unknown)"); };
 std::string vendor = safeAL(al.alGetString(AL_VENDOR));
 std::string renderer = safeAL(al.alGetString(AL_RENDERER));
 std::string version = safeAL(al.alGetString(AL_VERSION));
 std::string devName = safeAL(al.alcGetString(device, ALC_DEVICE_SPECIFIER));
 Log((std::string("OpenAL runtime identity: vendor=") + vendor + "; renderer=" + renderer + "; version=" + version + "; device=" + devName).c_str());
 if (!LooksLikeCreative(vendor, renderer, devName)) {
   Log("Creative OpenAL mode unsupported for this runtime/device; WASAPI audio is left on the original output path.");
   al.alcMakeContextCurrent(nullptr); al.alcDestroyContext(context); al.alcCloseDevice(device); context=nullptr; device=nullptr; return false;
 }
 creativeOpenALConfirmed.store(true, std::memory_order_release);
 bool mc=al.alIsExtensionPresent("AL_EXT_MCFORMATS")!=0;
 bool flt=al.alIsExtensionPresent("AL_EXT_float32")!=0;
 al.alcMakeContextCurrent(nullptr);
 outputFloat=flt;
 bool canMulti=mc && (inCh==4 || inCh==6 || inCh==7 || inCh==8);
 if (inCh>2 && !canMulti) { channels=2; Log("OpenAL multichannel format/layout unsupported; input will be downmixed to stereo."); }
 else channels=inCh;
 if (outputFloat) {
   if(channels==1) alFormat=AL_FORMAT_MONO_FLOAT32; else if(channels==2) alFormat=AL_FORMAT_STEREO_FLOAT32;
   else if(channels==4) alFormat=AL_FORMAT_QUAD32; else if(channels==6) alFormat=AL_FORMAT_51CHN32; else if(channels==7) alFormat=AL_FORMAT_61CHN32; else if(channels==8) alFormat=AL_FORMAT_71CHN32;
 } else {
   if(channels==1) alFormat=AL_FORMAT_MONO16; else if(channels==2) alFormat=AL_FORMAT_STEREO16;
   else if(channels==4) alFormat=0x1205; else if(channels==6) alFormat=0x120B; else if(channels==7) alFormat=0x120E; else if(channels==8) alFormat=0x1211;
 }
 alReady=true; workerStop=false; worker=std::thread(OutputThread);
 for (int i=0;i<100 && !outputReady.load();++i) Sleep(2);
 if (!outputReady.load()) { Log("OpenAL output thread did not become ready; original output retained."); workerStop=true; if(worker.joinable()) worker.join(); al.alcDestroyContext(context); al.alcCloseDevice(device); alReady=false; return false; }
 char msg[200]; sprintf_s(msg,"OpenAL redirect ready: %u Hz, input %u ch/%u-bit %s, output %u ch %s. Capture samples are converted; physical output format is device-dependent.",sampleRate,inputChannels,inputBits,inputFloat?"float":"PCM",channels,outputFloat?"float32":"PCM16"); Log(msg);
 return true;
}

float ReadSample(const uint8_t* p) {
 double v=0.0;
 if (inputFloat && inputBits==32) { float f; memcpy(&f,p,4); v=std::isfinite(f)?f:0.0; }
 else if (inputFloat && inputBits==64) { double d; memcpy(&d,p,8); v=std::isfinite(d)?d:0.0; }
 else if (inputBits==8) v=(static_cast<int>(*p)-128)/128.0;
 else if (inputBits==16) { int16_t x; memcpy(&x,p,2); v=x/32768.0; }
 else if (inputBits==24) { int32_t x=(int32_t(p[0]) | (int32_t(p[1])<<8) | (int32_t(p[2])<<16)); if(x&0x800000)x|=~0xffffff; v=x/8388608.0; }
 else if (inputBits==32) { int32_t x; memcpy(&x,p,4); v=x/2147483648.0; }
 return static_cast<float>(std::clamp(v,-1.0,1.0));
}

bool ConvertBuffer(const BYTE* src, UINT32 frames, std::vector<uint8_t>& dst) {
 if (!src || !frames || !inputChannels || !channels) return false;
 const size_t inBytes=inputBits/8, inFrame=inBytes*inputChannels;
 const size_t outBytes=outputFloat?sizeof(float):sizeof(int16_t);
 if (!inBytes || frames > SIZE_MAX/(channels*outBytes)) return false;
 dst.resize(static_cast<size_t>(frames)*channels*outBytes);
 auto put=[&](size_t i,float v) { if(outputFloat) reinterpret_cast<float*>(dst.data())[i]=v; else { int16_t q=static_cast<int16_t>(std::lrint(std::clamp(v,-1.0f,1.0f)*32767.0f)); memcpy(dst.data()+i*sizeof(int16_t),&q,sizeof(q)); } };
 for (UINT32 f=0; f<frames; ++f) {
   const uint8_t* frame=src+static_cast<size_t>(f)*inFrame;
   float samples[8]{}; for(unsigned c=0;c<inputChannels;++c) samples[c]=ReadSample(frame+c*inBytes);
   if(channels==inputChannels) { for(unsigned c=0;c<channels;++c) put(static_cast<size_t>(f)*channels+c,samples[c]); continue; }
   // WAVEFORMATEXTENSIBLE speaker-bit order: FL FR FC LFE BL BR FLC FRC BC SL SR.
   // For stereo fallback, use conservative ITU-style coefficients and avoid boosting summed peaks.
   float l=0,r=0; unsigned idx=0;
   if(channelMask) {
     const DWORD bits[]={1,2,4,8,16,32,64,128,256,512,1024};
     for(DWORD b:bits) if(channelMask&b) { if(idx>=inputChannels) break; float x=samples[idx++];
       switch(b){case 1:l+=x;break;case 2:r+=x;break;case 4:l+=x*0.70710678f;r+=x*0.70710678f;break;case 8:l+=x*0.5f;r+=x*0.5f;break;case 16:case 512:l+=x*0.70710678f;break;case 32:case 1024:r+=x*0.70710678f;break;case 64:l+=x*0.5f;break;case 128:r+=x*0.5f;break;case 256:l+=x*0.5f;r+=x*0.5f;break;default:break;}
     }
   } else { l=samples[0]; r=inputChannels>1?samples[1]:samples[0]; }
   put(static_cast<size_t>(f)*2,std::clamp(l,-1.0f,1.0f)); put(static_cast<size_t>(f)*2+1,std::clamp(r,-1.0f,1.0f));
 }
 return true;
}

// COM vtable patching. Original functions are retained once per interface implementation.
template<class T> bool PatchSlot(T* obj, size_t slot, void* replacement, void** original) {
 if (!obj) return false; void** vt=*reinterpret_cast<void***>(obj); DWORD old=0;
 if (!VirtualProtect(&vt[slot],sizeof(void*),PAGE_EXECUTE_READWRITE,&old)) return false;
 if (original && !*original) *original=vt[slot]; vt[slot]=replacement; DWORD ignored=0; VirtualProtect(&vt[slot],sizeof(void*),old,&ignored); FlushInstructionCache(GetCurrentProcess(),&vt[slot],sizeof(void*)); return true;
}
using CoCreateFn = HRESULT (WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*); CoCreateFn origCoCreate=nullptr;
using EnumFn = HRESULT (STDMETHODCALLTYPE*)(IMMDeviceEnumerator*, EDataFlow, DWORD, IMMDeviceCollection**);
using DefaultFn = HRESULT (STDMETHODCALLTYPE*)(IMMDeviceEnumerator*, EDataFlow, ERole, IMMDevice**);
using GetDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IMMDeviceEnumerator*, LPCWSTR, IMMDevice**);
using ItemFn = HRESULT (STDMETHODCALLTYPE*)(IMMDeviceCollection*, UINT, IMMDevice**);
using ActivateFn = HRESULT (STDMETHODCALLTYPE*)(IMMDevice*, REFIID, DWORD, PROPVARIANT*, void**);
using InitializeFn = HRESULT (STDMETHODCALLTYPE*)(IAudioClient*, AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID);
using GetServiceFn = HRESULT (STDMETHODCALLTYPE*)(IAudioClient*, REFIID, void**);
using GetBufferFn = HRESULT (STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, BYTE**);
using ReleaseBufferFn = HRESULT (STDMETHODCALLTYPE*)(IAudioRenderClient*, UINT32, DWORD);
EnumFn origEnum=nullptr; DefaultFn origDefault=nullptr; GetDeviceFn origGetDevice=nullptr; ItemFn origItem=nullptr; ActivateFn origActivate=nullptr;
InitializeFn origInitialize=nullptr; GetServiceFn origGetService=nullptr; GetBufferFn origGetBuffer=nullptr; ReleaseBufferFn origReleaseBuffer=nullptr;
// FMOD Ex 4.x (including the 4.22.00 version shown in the user's crash log) has a native
// OpenAL output backend. FMOD_OUTPUTTYPE_OPENAL is enum value 8 in FMOD Ex 4.24 headers;
// this legacy 4.x layout is used here only for FMOD Ex 4.x imports.
using FmodResult = int;
using FmodSystemCreateFn = FmodResult (__stdcall*)(void**);
using FmodSystemSetOutputFn = FmodResult (__stdcall*)(void*, int);
FmodSystemCreateFn origFmodSystemCreate = nullptr;
FmodSystemSetOutputFn origFmodSystemSetOutput = nullptr;
constexpr int FMOD_OUTPUTTYPE_OPENAL = 8;
std::atomic<bool> fmodSeen{false};
FmodResult __stdcall HookFmodSystemSetOutput(void* system, int output);
FmodResult __stdcall HookFmodSystemCreate(void** system);

void HookDevice(IMMDevice* d); void HookClient(IAudioClient* c); void HookRender(IAudioRenderClient* r);
HRESULT STDMETHODCALLTYPE HookItem(IMMDeviceCollection* s, UINT i, IMMDevice** d);
HRESULT STDMETHODCALLTYPE HookEnum(IMMDeviceEnumerator* s,EDataFlow f,DWORD m,IMMDeviceCollection** c) { HRESULT h=origEnum(s,f,m,c); if(SUCCEEDED(h)&&c&&*c){ void** vt=*reinterpret_cast<void***>(*c); if(!origItem) origItem=(ItemFn)vt[4]; PatchSlot(*c,4,(void*)HookItem,nullptr); } return h; }
HRESULT STDMETHODCALLTYPE HookItem(IMMDeviceCollection* s, UINT i, IMMDevice** d) { HRESULT h=origItem(s,i,d); if(SUCCEEDED(h)&&d&&*d) HookDevice(*d); return h; }
HRESULT STDMETHODCALLTYPE HookDefault(IMMDeviceEnumerator* s,EDataFlow f,ERole r,IMMDevice** d) { HRESULT h=origDefault(s,f,r,d); if(SUCCEEDED(h)&&d&&*d) HookDevice(*d); return h; }
HRESULT STDMETHODCALLTYPE HookGetDevice(IMMDeviceEnumerator* s,LPCWSTR id,IMMDevice** d) { HRESULT h=origGetDevice(s,id,d); if(SUCCEEDED(h)&&d&&*d) HookDevice(*d); return h; }
HRESULT STDMETHODCALLTYPE HookActivate(IMMDevice* s,REFIID iid,DWORD cls,PROPVARIANT* p,void** out) {
 HRESULT h=origActivate(s,iid,cls,p,out);
 if(SUCCEEDED(h)&&out&&*out&&IsEqualIID(iid,__uuidof(IAudioClient))) {
   auto c=reinterpret_cast<IAudioClient*>(*out); HookClient(c);
 }
 return h;
}
HRESULT STDMETHODCALLTYPE HookGetService(IAudioClient* s, REFIID iid, void** out);
HRESULT STDMETHODCALLTYPE HookInitialize(IAudioClient* s,AUDCLNT_SHAREMODE mode,DWORD flags,REFERENCE_TIME dur,REFERENCE_TIME period,const WAVEFORMATEX* fmt,LPCGUID ses) {
   bool supported=SetupOpenAL(fmt); HRESULT h=origInitialize(s,mode,flags,dur,period,fmt,ses);
   if(SUCCEEDED(h)) { { std::lock_guard<std::mutex> g(stateMutex); clientBridge[s]=supported; } void** vt=*reinterpret_cast<void***>(s); if(!origGetService) origGetService=(GetServiceFn)vt[14]; PatchSlot(s,14,(void*)HookGetService,nullptr); if(supported) Log("IAudioClient initialized; OpenAL redirect eligible."); }
   return h;
}
HRESULT STDMETHODCALLTYPE HookGetService(IAudioClient* s,REFIID iid,void** out) {
 HRESULT h=origGetService(s,iid,out); if(SUCCEEDED(h)&&out&&*out&&IsEqualIID(iid,__uuidof(IAudioRenderClient))) { auto r=reinterpret_cast<IAudioRenderClient*>(*out); bool enabled=false; { std::lock_guard<std::mutex> g(stateMutex); auto it=clientBridge.find(s); enabled=(it!=clientBridge.end() && it->second); } if(enabled) { { std::lock_guard<std::mutex> g(stateMutex); renderBridge.insert(r); } HookRender(r); } } return h;
}
thread_local BYTE* tlsBuffer=nullptr; thread_local UINT32 tlsFrames=0;
HRESULT STDMETHODCALLTYPE HookGetBuffer(IAudioRenderClient* s,UINT32 frames,BYTE** data) { HRESULT h=origGetBuffer(s,frames,data); if(SUCCEEDED(h)){tlsBuffer=(data?*data:nullptr);tlsFrames=frames;} return h; }
HRESULT STDMETHODCALLTYPE HookReleaseBuffer(IAudioRenderClient* s,UINT32 frames,DWORD flags) {
  bool copied=false;
  bool enabled=false; { std::lock_guard<std::mutex> g(stateMutex); enabled=renderBridge.find(s)!=renderBridge.end(); }
  if(enabled && outputReady.load(std::memory_order_acquire) && tlsBuffer && frames==tlsFrames && !(flags&AUDCLNT_BUFFERFLAGS_SILENT)) {
    Chunk c;
    if(ConvertBuffer(tlsBuffer,frames,c.bytes)) {
      { std::lock_guard<std::mutex> g(qMutex); if(queue.size()<MAX_QUEUE){queue.emplace_back(std::move(c));copied=true;} }
      if(copied) { memset(tlsBuffer,0,static_cast<size_t>(frames)*inputChannels*(inputBits/8)); }
    }
  }
  tlsBuffer=nullptr; tlsFrames=0;
  return origReleaseBuffer(s,frames,flags);
}

// Hook functions are installed lazily when COM objects are returned.
void HookEnumerator(IMMDeviceEnumerator* e) {
 if(!e) return; void** vt=*reinterpret_cast<void***>(e);
 if(!origEnum) { origEnum=(EnumFn)vt[3]; origDefault=(DefaultFn)vt[4]; origGetDevice=(GetDeviceFn)vt[5]; }
 PatchSlot(e,3,(void*)HookEnum,nullptr); PatchSlot(e,4,(void*)HookDefault,nullptr); PatchSlot(e,5,(void*)HookGetDevice,nullptr);
}
void HookDevice(IMMDevice* d) { if(!d) return; void** vt=*reinterpret_cast<void***>(d); if(!origActivate) origActivate=(ActivateFn)vt[3]; PatchSlot(d,3,(void*)HookActivate,nullptr); }
void HookClient(IAudioClient* c) { if(!c) return; void** vt=*reinterpret_cast<void***>(c); if(!origInitialize) origInitialize=(InitializeFn)vt[3]; PatchSlot(c,3,(void*)HookInitialize,nullptr); }
void HookRender(IAudioRenderClient* r) { if(!r) return; void** vt=*reinterpret_cast<void***>(r); if(!origGetBuffer) origGetBuffer=(GetBufferFn)vt[3]; if(!origReleaseBuffer) origReleaseBuffer=(ReleaseBufferFn)vt[4]; PatchSlot(r,3,(void*)HookGetBuffer,nullptr); PatchSlot(r,4,(void*)HookReleaseBuffer,nullptr); }

FmodResult __stdcall HookFmodSystemSetOutput(void* system, int output) {
    if (!origFmodSystemSetOutput) return 31; // FMOD_ERR_UNSUPPORTED
    if (!openALDeviceProbeOk.load(std::memory_order_acquire)) {
        Log("FMOD System_SetOutput left unchanged because the system OpenAL device probe failed.");
        return origFmodSystemSetOutput(system, output);
    }
    if (output != FMOD_OUTPUTTYPE_OPENAL) {
        char msg[160]{}; sprintf_s(msg, "FMOD System_SetOutput requested type %d; forcing legacy FMOD Ex OpenAL type %d.", output, FMOD_OUTPUTTYPE_OPENAL); Log(msg);
    }
    FmodResult r = origFmodSystemSetOutput(system, FMOD_OUTPUTTYPE_OPENAL);
    char msg[160]{}; sprintf_s(msg, "FMOD System_SetOutput(OpenAL) returned %d.", r); Log(msg);
    return r;
}
FmodResult __stdcall HookFmodSystemCreate(void** system) {
    if (!origFmodSystemCreate) return 31;
    FmodResult r = origFmodSystemCreate(system);
    if (r == 0 && system && *system) {
        fmodSeen.store(true);
        if (!openALDeviceProbeOk.load(std::memory_order_acquire)) {
            Log("FMOD Ex detected, but OpenAL device probe failed; leaving FMOD output at the game's requested/default backend.");
            return r;
        }
        if (!origFmodSystemSetOutput) {
            HMODULE fmod = GetModuleHandleW(L"fmodex.dll");
            if (fmod) origFmodSystemSetOutput = reinterpret_cast<FmodSystemSetOutputFn>(GetProcAddress(fmod, "FMOD_System_SetOutput"));
        }
        if (origFmodSystemSetOutput) {
            FmodResult sr = origFmodSystemSetOutput(*system, FMOD_OUTPUTTYPE_OPENAL);
            char msg[192]{}; sprintf_s(msg, "FMOD_System_Create succeeded; requested native OpenAL output (type %d), SetOutput result=%d.", FMOD_OUTPUTTYPE_OPENAL, sr); Log(msg);
        } else Log("FMOD_System_Create detected, but FMOD_System_SetOutput export was not found; native OpenAL forcing unavailable.");
    }
    return r;
}

HRESULT WINAPI HookCoCreateInstance(REFCLSID cls,LPUNKNOWN outer,DWORD ctx,REFIID iid,LPVOID* out) {
 HRESULT h=origCoCreate(cls,outer,ctx,iid,out);
 if(SUCCEEDED(h)&&out&&*out&&IsEqualIID(iid,__uuidof(IMMDeviceEnumerator))) { Log("WASAPI IMMDeviceEnumerator created; WASAPI interception path is active."); HookEnumerator(reinterpret_cast<IMMDeviceEnumerator*>(*out)); }
 return h;
}
// Patch imports for WASAPI, legacy FMOD Ex, and static OpenAL32.dll calls in already loaded modules.
void PatchImports(HMODULE mod) {
 if(!mod) return; auto base=(uint8_t*)mod; auto dos=(IMAGE_DOS_HEADER*)base; if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return;
 auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew); if(nt->Signature!=IMAGE_NT_SIGNATURE) return;
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if(!dir.VirtualAddress) return;
 auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
 for(;imp->Name; ++imp) {
   auto names=(IMAGE_THUNK_DATA*)(base+imp->OriginalFirstThunk); auto funcs=(IMAGE_THUNK_DATA*)(base+imp->FirstThunk); if(!imp->OriginalFirstThunk) continue;
   const char* importedDll = (const char*)(base + imp->Name);
   bool openALDll = (_stricmp(importedDll, "openal32.dll") == 0);
   if (openALDll) {
     if (!creativeOpenALConfirmed.load(std::memory_order_acquire) || !al.dll) continue;
     bool allExportsAvailable = true;
     for (auto* check = names; check->u1.AddressOfData; ++check) {
       if (IMAGE_SNAP_BY_ORDINAL(check->u1.Ordinal)) { allExportsAvailable = false; break; }
       auto* checkName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + check->u1.AddressOfData);
       if (!GetProcAddress(al.dll, reinterpret_cast<const char*>(checkName->Name))) { allExportsAvailable = false; break; }
     }
     if (!allExportsAvailable) {
       if (!openALImportIncompatibleLogged.exchange(true)) Log("An OpenAL32.dll import table requests exports missing from the validated Creative runtime; that module is left unchanged to avoid mixing OpenAL implementations.");
       continue;
     }
   }
   for(;names->u1.AddressOfData; ++names,++funcs) {
     if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
     auto byName=(IMAGE_IMPORT_BY_NAME*)(base+names->u1.AddressOfData);
     const char* importName = (const char*)byName->Name;
     bool legacyFmodEx = (_stricmp(importedDll, "fmodex.dll") == 0);
     void* replacement = nullptr;
     if (openALDll && creativeOpenALConfirmed.load(std::memory_order_acquire) && al.dll) {
       replacement = reinterpret_cast<void*>(GetProcAddress(al.dll, importName));
     }
     else if(strcmp(importName,"CoCreateInstance")==0) replacement = (void*)HookCoCreateInstance;
     else if(legacyFmodEx && strcmp(importName,"FMOD_System_Create")==0) replacement = (void*)HookFmodSystemCreate;
     else if(legacyFmodEx && strcmp(importName,"FMOD_System_SetOutput")==0) replacement = (void*)HookFmodSystemSetOutput;
     if (replacement) {
       DWORD old=0; if(VirtualProtect(&funcs->u1.Function,sizeof(void*),PAGE_READWRITE,&old)) {
         void* current=(void*)funcs->u1.Function;
         if (current != replacement) {
           if (replacement == (void*)HookCoCreateInstance && !origCoCreate) origCoCreate=(CoCreateFn)current;
           if (replacement == (void*)HookFmodSystemCreate && !origFmodSystemCreate) origFmodSystemCreate=(FmodSystemCreateFn)current;
           if (replacement == (void*)HookFmodSystemSetOutput && !origFmodSystemSetOutput) origFmodSystemSetOutput=(FmodSystemSetOutputFn)current;
           funcs->u1.Function=(ULONG_PTR)replacement;
           if (openALDll && !openALImportsRedirected.exchange(true)) Log("Static OpenAL32.dll imports are being redirected to the validated Creative OpenAL runtime.");
         }
         DWORD ignored=0; VirtualProtect(&funcs->u1.Function,sizeof(void*),old,&ignored);
       }
     }
   }
 }
}
void InstallImports() {
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId()); if(snap==INVALID_HANDLE_VALUE) return;
 MODULEENTRY32W me{}; me.dwSize=sizeof(me); if(Module32FirstW(snap,&me)) do { PatchImports(me.hModule); } while(Module32NextW(snap,&me)); CloseHandle(snap);
}
DWORD WINAPI InitThread(void*) {
 Sleep(50);
 // Preload the system OpenAL runtime before the game/FMOD initializes its output backend.
 // The probe is diagnostic; it does not keep a device open.
 ProbeOpenALDevice();
 // Modules such as fmodex.dll may be loaded after process start. Rescan imports briefly so
 // the FMOD output selection can be changed before the engine initializes its mixer.
 for (int i=0; i<80; ++i) { InstallImports(); if (fmodSeen.load()) break; Sleep(250); }
 InstallImports();
 Log("Hook initialization scan finished. Check this log for FMOD Ex OpenAL selection or WASAPI interception. If neither appears, this game's audio API was not intercepted.");
 return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID) {
 if(reason==DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); HANDLE t=CreateThread(nullptr,0,InitThread,nullptr,0,nullptr); if(t) CloseHandle(t); }
 if(reason==DLL_PROCESS_DETACH) { workerStop=true; /* Avoid joining threads or unloading OpenAL while the Windows loader lock is held. */ }
 return TRUE;
}
