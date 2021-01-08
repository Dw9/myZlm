/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xiongziliang/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <cctype>
#include <algorithm>
#include "RtspDemuxer.h"
#include "Util/base64.h"
#include "Extension/Factory.h"

using namespace std;
static const char* mySdp = 
"v=0\n"
"o=- 0 0 IN IP4 127.0.0.1\n"
"c=IN IP4 192.168.3.12\n"
"t=0 0\n"
"s=Streamed by ZLMediaKit-5.0\n"
"a=tool:libavformat 58.45.100\n"
"m=audio 0 RTP/AVP 96\n"
"b=AS:48\n"
"a=rtpmap:96 L16/8000/1\n"
"a=control:streamid=0\n";

// static const char* mySdp = 
// "v=0\n"
// "o=- 0 0 IN IP4 127.0.0.1\n"
// "c=IN IP4 192.168.3.12\n"
// "t=0 0\n"
// "s=Streamed by ZLMediaKit-5.0\n"
// "a=tool:libavformat 58.45.100\n"
// "m=audio 0 RTP/AVP 96\n"
// "b=AS:48\n"
// "a=rtpmap:96 mpeg4-generic/8000/1\n"
// "a=fmtp:96 profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3; config=158856E500\n"
// "a=control:streamid=0\n";

namespace mediakit {

void RtspDemuxer::loadSdp(const string &sdp){
    //loadSdp(SdpParser(sdp));
    loadSdp(SdpParser(mySdp));
         
    //std::cout<<"recv sdp origin"<<"\n"<<sdp<<endl;    
//     std::cout<<"recv sdp construction"<<"\n"<<mySdp<<endl;

}

void RtspDemuxer::loadSdp(const SdpParser &attr) {
    auto tracks = attr.getAvailableTrack();
    for (auto &track : tracks){
        switch (track->_type) {
            case TrackVideo: {
                makeVideoTrack(track);
            }
                break;
            case TrackAudio: {
                makeAudioTrack(track);
            }
                break;
            default:
                break;
        }
    }
    auto titleTrack = attr.getTrack(TrackTitle);
    if(titleTrack){
        _fDuration = titleTrack->_duration;
    }
}


bool RtspDemuxer::inputRtp(const RtpPacket::Ptr & rtp) {
    switch (rtp->type) {
    case TrackVideo:{
        if(_videoRtpDecoder){
            return _videoRtpDecoder->inputRtp(rtp, true);
        }
        return false;
    }
    case TrackAudio:{
        if(_audioRtpDecoder){
            _audioRtpDecoder->inputRtp(rtp, false);
            return false;
        }
        return false;
    }
    default:
        return false;
    }
}


void RtspDemuxer::makeAudioTrack(const SdpTrack::Ptr &audio) {
    //生成Track对象
    _audioTrack = dynamic_pointer_cast<AudioTrack>(Factory::getTrackBySdp(audio));
    if(_audioTrack){
        //生成RtpCodec对象以便解码rtp
        _audioRtpDecoder = Factory::getRtpDecoderByTrack(_audioTrack);
        if(_audioRtpDecoder){
            //设置rtp解码器代理，生成的frame写入该Track
            _audioRtpDecoder->addDelegate(_audioTrack);
            onAddTrack(_audioTrack);
        } else{
            //找不到相应的rtp解码器，该track无效
            _audioTrack.reset();
        }
    }
}

void RtspDemuxer::makeVideoTrack(const SdpTrack::Ptr &video) {
    //生成Track对象
    _videoTrack = dynamic_pointer_cast<VideoTrack>(Factory::getTrackBySdp(video));
    if(_videoTrack){
        //生成RtpCodec对象以便解码rtp
        _videoRtpDecoder = Factory::getRtpDecoderByTrack(_videoTrack);
        if(_videoRtpDecoder){
            //设置rtp解码器代理，生成的frame写入该Track
            _videoRtpDecoder->addDelegate(_videoTrack);
            onAddTrack(_videoTrack);
        }else{
            //找不到相应的rtp解码器，该track无效
            _videoTrack.reset();
        }
    }
}

} /* namespace mediakit */
