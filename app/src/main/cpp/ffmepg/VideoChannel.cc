//
// Created by Administrator on 2020\4\2 0002.
//
#include "android/log.h"
#include "exception"
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}
#include "../utils/ReleaseUtils.h"
#include "../utils/RenderCallBack.h"
#include "pthread.h"
#include "queue"

#define LOG_TAG "[nelson]"
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))

void *decode_task(void *args);
void *render_task(void *args);

/**
 * 丢已经解码的图片
 * @param q
 */
void dropAvFrame(std::queue<AVFrame *> &q) {
    if (!q.empty()) {
        AVFrame *frame = q.front();
        ReleaseUtils::releaseAvFrame(&frame);
        q.pop();
    }
}

/**
 * 线程睡眠(单位为毫秒)
 * @param nsec
 */
void m_threadSleep(long nsec) {
    struct timespec sleepTime;
    struct timespec returnTime;
    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = nsec * 1000000;
    nanosleep(&sleepTime, &returnTime);
}

#include "VideoChannel.h"
#include "../constant/Session.h"
VideoChannel::VideoChannel(int streamId,
                           AVRational time_base, Session *session,
                           double fps,
                           AVCodecContext
                           *pContext,
                           RenderFrameCallBack callBack) : BaseChannel(streamId,
                                                                       time_base,
                                                                       pContext) {

    this->session = session;

    frameQueue.setReleaseCallBack(ReleaseUtils::releaseAvFrame);

    frameQueue.setSyncHandle(dropAvFrame);
    this->fps = fps;

    this->frame_delays = 1 / fps;
//    // 获取图片转换器转换器
    swsContext = sws_getContext(pContext->width,
                                pContext->height,
                                AV_PIX_FMT_YUVJ420P,
                                pContext->width,
                                pContext->height,
                                AV_PIX_FMT_RGBA,
                                SWS_BILINEAR,
                                0,
                                0,
                                0);
//设置回调
    renderFrameCallBack = callBack;
}

VideoChannel::~VideoChannel() {
    channelIsWorking = false;
    packets.setWork(false);
    packets.clear();
    frameQueue.setWork(false);
    frameQueue.clear();
}

void VideoChannel::start() {
    channelIsWorking = true;
    packets.setWork(true);
    frameQueue.setWork(true);
    pthread_create(&decodeThreadId, 0, decode_task, this);
    pthread_create(&renderThreadId, 0, render_task, this);
}

void VideoChannel::stop() {
    channelIsWorking = false;
    packets.setWork(false);
    packets.clear();
    frameQueue.clear();
}

void VideoChannel::runDecodeTask() {
    LOGD("Run VideoDecodeTask");
    AVPacket *packet = 0;
    while (channelIsWorking) {
        while (frameQueue.size() > 200) {
            m_threadSleep(1000);
        }
        int ret = packets.pop(packet);
        if (!channelIsWorking) {
            break;
        }
        if (!ret) {
            continue;
        }
        // 将avPacket送至解码器进行解码
        ret = avcodec_send_packet(avCodecContext, packet);
        ReleaseUtils::releaseAvPacket(&packet);
        // 解码失败
        if (ret != 0) {
            LOGD("Send Packet Error");
            break;
        }
        // 代表了某一帧
        AVFrame *frame = av_frame_alloc();
        // 从解码器中读取解码后的数据包
        ret = avcodec_receive_frame(avCodecContext, frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret != 0) {
            break;
        }
        frameQueue.push(frame);
    }
    LOGD("Decode Thread Finish");
}

void VideoChannel::runRenderTask() {

    uint8_t *dst_data[4];
    int dst_lineSize[4];
    av_image_alloc(dst_data, dst_lineSize,
                   avCodecContext->width, avCodecContext->height, AV_PIX_FMT_RGBA, 1);
    AVFrame *frame = 0;
    while (channelIsWorking) {
        int ret = frameQueue.pop(frame);
        if (!channelIsWorking) {
            break;
        }
        if (ret) {
            sws_scale(swsContext,
                      frame->data,
                      frame->linesize,
                      0,
                      avCodecContext->height,
                      dst_data,
                      dst_lineSize);
        }

        double clock = frame->best_effort_timestamp * av_q2d(time_base);
        //额外的间隔时间
        double extra_delay = frame->repeat_pict / (2 * fps);
        // 真实需要的间隔时间
        double delays = extra_delay + frame_delays;
        if (clock == 0) {
            m_threadSleep(frame_delays);
        } else {
            //间隔 音视频相差的间隔
            double diff = clock - session->audio_clock;
            if (diff > 0) {
                //大于0 表示视频比较快
                m_threadSleep((delays + diff) * 1000);
            } else if (diff < 0) {
                //小于0 表示音频比较快
                // 视频包积压的太多了 （丢包）
                if (fabs(diff) >= 0.05) {
                    ReleaseUtils::releaseAvFrame(&frame);
                    //丢包
                    frameQueue.sync();
                    continue;
                } else {
                    //不睡了 快点赶上 音频
                }
            }
        }

        if (renderFrameCallBack) {
            renderFrameCallBack(dst_data[0],
                                dst_lineSize[0],
                                avCodecContext->width,
                                avCodecContext->height);
        }
        ReleaseUtils::releaseAvFrame(&frame);
    }
    av_freep(&dst_data[0]);
    channelIsWorking = false;
    ReleaseUtils::releaseAvFrame(&frame);
    sws_freeContext(swsContext);
}

void *decode_task(void *args) {
    VideoChannel *videoChannel = static_cast<VideoChannel *>(args);
    videoChannel->runDecodeTask();
    return NULL;
}
void *render_task(void *args) {
    VideoChannel *videoChannel = static_cast<VideoChannel *>(args);
    videoChannel->runRenderTask();
    return NULL;
}