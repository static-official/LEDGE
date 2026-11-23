// LEDGE iOS — platform host implementation (Objective-C++).
// WebSocket: URLSessionWebSocketTask (iOS 13+). HTTPS: NSURLSession dataTask
// with a semaphore (playfab.cpp calls this off-thread). Audio: AVAudioPlayer
// playlist (music/*.m4a) + a 6-voice pool of AVAudioPlayers (sfx/*.m4a).
#import "ios_host.h"
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#include "../ledgevr/native/log.h"
#include <cstring>

namespace lv {

static IosHost g_host;
IosHost& jni() { return g_host; }

// ---------------------------------------------------------------------------
// WebSocket bridge
// ---------------------------------------------------------------------------
@interface BridgeWS : NSObject <NSURLSessionWebSocketDelegate>
@property (strong) NSURLSession* session;
@property (strong) NSURLSessionWebSocketTask* task;
@end
@implementation BridgeWS
- (void)URLSession:(NSURLSession*)s webSocketTask:(NSURLSessionWebSocketTask*)t
    didOpenProtocolWithProtocolString:(NSString*)p {
    (void)s; (void)t; (void)p;
    lv::jni().ready_ = true;
    LOGI("iOS ws opened");
}
- (void)URLSession:(NSURLSession*)s webSocketTask:(NSURLSessionWebSocketTask*)t
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData*)reason {
    (void)t; (void)reason;
    std::lock_guard<std::mutex> lk(lv::jni().queue_.mu);
    lv::jni().queue_.closed = true;
    lv::jni().queue_.closeReason = "code " + std::to_string((int)code);
    LOGI("iOS ws closed %d", (int)code);
}
- (void)URLSession:(NSURLSession*)s task:(NSURLSessionTask*)t
    didCompleteWithError:(NSError*)err {
    (void)s; (void)t;
    std::lock_guard<std::mutex> lk(lv::jni().queue_.mu);
    lv::jni().queue_.closed = true;
    lv::jni().queue_.closeReason = err.localizedDescription ? std::string(err.localizedDescription.UTF8String) : "error";
    if (err) LOGE("iOS ws error: %s", err.localizedDescription.UTF8String);
}
@end

static void wsReceiveLoop(BridgeWS* ws) {
    if (!ws || !ws.task) return;
    __weak BridgeWS* weakWs = ws;
    [ws.task receiveMessageWithCompletionHandler:
        ^(NSURLSessionWebSocketMessage* msg, NSError* err) {
            if (err) {
                std::lock_guard<std::mutex> lk(lv::jni().queue_.mu);
                lv::jni().queue_.closed = true;
                return;
            }
            if (msg.data) {
                NSData* d = msg.data;
                std::vector<uint8_t> bytes((const uint8_t*)d.bytes,
                                           (const uint8_t*)d.bytes + d.length);
                std::lock_guard<std::mutex> lk(lv::jni().queue_.mu);
                if (lv::jni().queue_.frames.size() < 256)
                    lv::jni().queue_.frames.push_back(std::move(bytes));
            }
            if (BridgeWS* strong = weakWs) wsReceiveLoop(strong);
        }];
}

void IosHost::init() {
    // playback category so music/SFX mix into the game
    try {
        NSError* err = nil;
        [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback
                                         mode:AVAudioSessionModeGameChat
                                      options:0 error:&err];
        [[AVAudioSession sharedInstance] setActive:YES error:&err];
    } catch (...) {}
    audioInit();
    ready_ = true;
    LOGI("iOS host ready");
}

void IosHost::shutdown() {
    wsClose();
}

bool IosHost::wsConnect(const std::string& url) {
    wsClose();
    BridgeWS* ws = [BridgeWS new];
    ws.session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]
                                           delegate:ws delegateQueue:nil];
    NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!u) return false;
    ws.task = [ws.session webSocketTaskWithURL:u];
    [ws.task resume];
    ws_ = (__bridge_retained void*)ws;    // host owns it
    {
        std::lock_guard<std::mutex> lk(queue_.mu);
        queue_.frames.clear();
        queue_.closed = false;
    }
    wsReceiveLoop(ws);
    return true;
}

void IosHost::wsSend(const std::vector<uint8_t>& data) {
    if (!ws_ || data.empty()) return;
    BridgeWS* ws = (__bridge BridgeWS*)ws_;
    if (!ws.task) return;
    NSData* d = [NSData dataWithBytes:data.data() length:data.size()];
    NSURLSessionWebSocketMessage* m = [[NSURLSessionWebSocketMessage alloc] initWithData:d];
    [ws.task sendMessage:m completionHandler:^(NSError* err) {
        if (err) LOGE("iOS ws send failed: %s", err.localizedDescription.UTF8String);
    }];
}

void IosHost::wsClose() {
    if (!ws_) return;
    BridgeWS* ws = (__bridge_transfer BridgeWS*)ws_;
    ws_ = nullptr;
    [ws.task closeWithCode:NSURLSessionWebSocketCloseCodeGoingAway reason:nil];
    [ws.session invalidateAndCancel];
    {
        std::lock_guard<std::mutex> lk(queue_.mu);
        queue_.closed = true;
    }
}

bool IosHost::wsRecv(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(queue_.mu);
    if (queue_.frames.empty()) return false;
    out = std::move(queue_.frames.front());
    queue_.frames.pop_front();
    return true;
}

std::string IosHost::httpPost(const std::string& url, const std::string& body,
                              const std::string& headers, int& httpCode) {
    httpCode = 0;
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:
        [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];
    if (!req) return "bad url";
    req.HTTPMethod = @"POST";
    req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];
    size_t pos = 0;
    while (pos < headers.size()) {
        size_t e = headers.find('\n', pos);
        std::string line = headers.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        size_t c = line.find(':');
        if (c != std::string::npos && c > 0)
            [req setValue:[NSString stringWithUTF8String:line.substr(c + 1).c_str()]
              forHTTPHeaderField:[NSString stringWithUTF8String:line.substr(0, c).c_str()]];
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    __block NSData* out = nil;
    __block NSString* errText = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:req
        completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
            if (err) errText = err.localizedDescription;
            else {
                NSHTTPURLResponse* http = (NSHTTPURLResponse*)resp;
                httpCode = (int)http.statusCode;
                out = data;
            }
            dispatch_semaphore_signal(sem);
        }];
    [task resume];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 20LL * NSEC_PER_SEC));
    if (out) return std::string((const char*)out.bytes, out.length);
    return errText ? std::string(errText.UTF8String) : "transport failure";
}

// ---------------------------------------------------------------------------
// audio: AVAudioPlayer playlist + SFX pool
// ---------------------------------------------------------------------------
static const char* kMusicFiles[7] = {
    "menu_candy_skies", "build_ledge_loops", "game_pastel_rush",
    "game_cloud_hop", "game_neon_climb", "game_bubble_bass", "credits_star_splash",
};
static const char* kSfxFiles[13] = {
    "grab", "release", "land", "goal", "bounce", "place", "delete",
    "click", "open", "success", "whoosh", "pop", "emote"
};

@interface AudioIOS : NSObject <AVAudioPlayerDelegate>
@property (strong) NSMutableArray<AVAudioPlayer*>* pool;   // 13 sfx players
@property (strong) AVAudioPlayer* music;
@property (assign) int track;
@property (assign) float musicVol, sfxVol;
- (void)playMusic:(int)idx;
- (void)playSfx:(int)idx;
@end
static AudioIOS* gAudio = nil;

@implementation AudioIOS
- (instancetype)init {
    if (self = [super init]) {
        _pool = [NSMutableArray new];
        for (int i = 0; i < 13; i++) {
            NSURL* u = [NSURL fileURLWithPath:
                [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:kSfxFiles[i]]
                                                ofType:@"m4a" inDirectory:@"sfx"]];
            if (!u) u = [NSURL fileURLWithPath:
                [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:kSfxFiles[i]]
                                                ofType:@"m4a"]];
            if (u) {
                NSError* err = nil;
                AVAudioPlayer* p = [[AVAudioPlayer alloc] initWithContentsOfURL:u error:&err];
                if (p) {
                    p.volume = 1.0;
                    [p prepareToPlay];
                    [_pool addObject:p];
                } else {
                    [_pool addObject:[NSNull null]];
                }
            } else {
                [_pool addObject:[NSNull null]];
            }
        }
        _track = 0;
        _musicVol = 0.8f;
        _sfxVol = 1.0f;
    }
    return self;
}
- (void)playMusic:(int)idx {
    self.track = ((idx % 7) + 7) % 7;
    if (self.music) { [self.music stop]; self.music = nil; }
    NSURL* u = [NSURL fileURLWithPath:
        [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:kMusicFiles[self.track]]
                                        ofType:@"m4a" inDirectory:@"music"]];
    if (!u) u = [NSURL fileURLWithPath:
        [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:kMusicFiles[self.track]]
                                        ofType:@"m4a"]];
    if (!u) return;
    NSError* err = nil;
    self.music = [[AVAudioPlayer alloc] initWithContentsOfURL:u error:&err];
    if (!self.music) return;
    self.music.numberOfLoops = 0;
    self.music.volume = self.musicVol;
    [self.music play];
    __weak AudioIOS* wself = self;
    self.music.delegate = self;
    (void)wself;
}
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer*)player successfully:(BOOL)flag {
    (void)player; (void)flag;
    [self playMusic:(self.track + 1) % 7];    // loop the playlist
}
- (void)playSfx:(int)idx {
    if (idx < 0 || idx >= 13) return;
    id p = self.pool[idx];
    if (p == [NSNull null] || !p) return;
    AVAudioPlayer* ap = (AVAudioPlayer*)p;
    ap.currentTime = 0;
    ap.volume = self.sfxVol;
    [ap play];
}
@end

void IosHost::audioInit() {
    if (!gAudio) gAudio = [AudioIOS new];
}

void IosHost::musicPlay(int trackIdx) { if (gAudio) [gAudio playMusic:trackIdx]; }
void IosHost::musicNext() { if (gAudio) [gAudio playMusic:(gAudio.track + 1) % 7]; }
void IosHost::musicStop() { if (gAudio.music) { [gAudio.music stop]; gAudio.music = nil; } }
void IosHost::sfx(int idx) { if (gAudio) [gAudio playSfx:idx]; }
void IosHost::musicVol(float v) { if (gAudio) { gAudio.musicVol = v; if (gAudio.music) gAudio.music.volume = v; } }
void IosHost::sfxVol(float v) { if (gAudio) gAudio.sfxVol = v; }
int IosHost::musicTrack() { return gAudio ? gAudio.track : 0; }

} // namespace lv
