//
// Created by Administrator on 2020\4\2 0002.
//

#include <pthread.h>
#include "Player.h"
#include "cstring"
#include "android/log.h"
#include "../constant/Global.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#define LOG_TAG "[nelson]"
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))

void *_prepare(void *args);

Player::Player(PlayerCallBack *callBack) {
    avcodec_register_all();
    av_register_all();
    avformat_network_init();
    this->callBack = callBack;
    this->avFormatContext = avformat_alloc_context();
}
Player::~Player() {

}
void Player::setDataSource(const char *dataSource) {
    //防止dataSource在其他地方被释放
    this->dataSource = new char[strlen(dataSource)];
    strcpy(this->dataSource, dataSource);
}
void Player::prepare() {
    pthread_t *prepareThread;
    pthread_create(prepareThread, NULL, _prepare, this);
}

void *_prepare(void *args) {
    Player *player = static_cast<Player *>(args);
    int ret = avformat_open_input(&player->avFormatContext, player->dataSource, NULL, NULL);
    if (ret != 0) {
        player->callBack->onError(THREAD_CHILD, FFMPEG_CAN_NOT_OPEN_URL);
        return NULL;
    }

    LOGD("Ret : %d", ret);
    return NULL;
}