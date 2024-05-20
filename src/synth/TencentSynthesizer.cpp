#include "TencentSynthesizer.h"
#include "Synthesizer.h"
#include <exception>
#include <mutex>

void OnSynthesisStart(SpeechSynthesisResponse* rsp)
{
    INFOLN("OnSynthesisStart, voiceId:%s", rsp->session_id.c_str());
}
// 识别失败回调
void OnSynthesisFail(SpeechSynthesisResponse* rsp)
{
    string voiceId = rsp->session_id;
    INFOLN("OnSynthesisFail, voiceId:%s code:%d msg:%s", rsp->session_id.c_str(), rsp->code, rsp->message.c_str());
    auto synthesizer = Synthesizer::GetSynthesizer(voiceId);
    if (!synthesizer) {
        WARNLN("synthesizer is NULL when OnSynthesisEnd, voiceId:%s", voiceId.c_str());
        return;
    }
    synthesizer->onSynthesisEnd();
}

// 文本结果回调
void OnTextResult(SpeechSynthesisResponse* rsp)
{
    // 处理文本结果
    std::string session_id = rsp->session_id;
    std::string message_id = rsp->message_id;
    std::string request_id = rsp->request_id;
    std::vector<Subtitle> subtitles = rsp->result.subtitles;
    std::ostringstream oss;
    for (std::vector<Subtitle>::iterator it = subtitles.begin(); it != subtitles.end(); it++) {
        std::string text = it->text;
        oss << it->begin_index << "|" << it->end_index << "|"
            << it->begin_time << "|" << it->end_time << "|"
            << text << "|" << it->phoneme << std::endl;
    }
    INFOLN("OnTextResult, voiceId:%s message_id:%s request_id:%s result:%s", session_id.c_str(), message_id.c_str(), request_id.c_str(), oss.str().c_str());
}

// 音频结果回调
void OnAudioResult(SpeechSynthesisResponse* rsp)
{
    string voiceId = rsp->session_id;
    std::string& audio_data = rsp->data;
    auto synthesizer = Synthesizer::GetSynthesizer(voiceId);
    if (!synthesizer) {
        return;
    }
    synthesizer->pushData((char*)audio_data.c_str(), audio_data.size());
}

// 识别完成回调
void OnSynthesisEnd(SpeechSynthesisResponse* rsp)
{
    string voiceId = rsp->session_id;
    std::string& audio_data = rsp->data;
    INFOLN("OnSynthesisEnd, voiceId:%s audio_data len:%d", rsp->session_id.c_str(), audio_data.size());
    auto synthesizer = Synthesizer::GetSynthesizer(voiceId);
    if (!synthesizer) {
        WARNLN("synthesizer is NULL when OnSynthesisEnd, voiceId:%s", voiceId.c_str());
        return;
    }
    if (audio_data.size() > 0) {
        synthesizer->pushData((char*)audio_data.c_str(), audio_data.size());
    }
    synthesizer->onSynthesisEnd();
}

TencentSynthesizer::~TencentSynthesizer()
{
    stop();
    INFOLN("TencentSynthesizer destruct, channelId:%s voiceId:%s", mChannelId.c_str(), mVoiceId.c_str());
}

int TencentSynthesizer::init()
{
    loadConfig();
    SpeechSynthesizer* synthesizer = new SpeechSynthesizer(mAppId, mSecretId, mSecretKey, mVoiceId);
    synthesizer->SetOnSynthesisStart(OnSynthesisStart);
    synthesizer->SetOnSynthesisFail(OnSynthesisFail);
    synthesizer->SetOnSynthesisEnd(OnSynthesisEnd);
    synthesizer->SetOnTextResult(OnTextResult);
    synthesizer->SetOnAudioResult(OnAudioResult);
    mAudioData.insert(mAudioData.end(), 160 * 5, 0);

    uint64_t voiceType = 1001;
    try {
        voiceType = std::stoul(mVoiceName);
    } catch (std::exception& e) {
        WARNLN("voiceName do not convert to long, voiceType:%ld voiceName:%s err:%s", voiceType, mVoiceName.c_str(), e.what());
    }
    synthesizer->SetVoiceType(voiceType);
    synthesizer->SetCodec("pcm");
    synthesizer->SetSampleRate(8000);
    synthesizer->SetSpeed(0);
    synthesizer->SetVolume(0);
    synthesizer->SetText(mText);
    synthesizer->SetEnableSubtitle(true);
    INFOLN("begin synthesizer start, voiceType:%ld channelId:%s voiceId:%s", voiceType, mChannelId.c_str(), mVoiceId.c_str());
    int ret = synthesizer->Start();
    if (ret < 0) {
        ERRLN("synthesizer start failed, ret:%d channelId:%s voiceId:%s", ret, mChannelId.c_str(), mVoiceId.c_str());
        delete synthesizer;
        return -1;
    }
    INFOLN("end synthesizer start, channelId:%s voiceId:%s", mChannelId.c_str(), mVoiceId.c_str());
    mSpeechSynthesizer.reset(synthesizer);
    return 0;
}

void TencentSynthesizer::stop()
{
    std::lock_guard<std::mutex> l(mMutex);
    if (mIsStop) {
        return;
    }
    mIsStop = true;
    if (mSpeechSynthesizer) {
        INFOLN("stop tencent recognize, channelId:%s", mChannelId.c_str());
        mSpeechSynthesizer->Stop("user stop");
    }
    mCv.notify_all();
}
