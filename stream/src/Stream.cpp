/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "QAL: Stream"
#include "Stream.h"
#include "StreamPCM.h"
#include "StreamCompress.h"
#include "StreamSoundTrigger.h"
#include "Session.h"
#include "SessionGsl.h"
#include "SessionAlsaPcm.h"
#include "ResourceManager.h"
#include "Device.h"

std::shared_ptr<ResourceManager> Stream::rm = nullptr;
std::mutex Stream::mBaseStreamMutex;
struct qal_device* Stream::mQalDevice = nullptr;

Stream* Stream::create(struct qal_stream_attributes *sAttr, struct qal_device *dAttr,
    uint32_t noOfDevices, struct modifier_kv *modifiers, uint32_t noOfModifiers)
{
    std::lock_guard<std::mutex> lock(mBaseStreamMutex);
    Stream* stream = NULL;
    int status = 0;
    uint32_t count = 0;
    bool isStandby = false;
    struct qal_ec_info ecinfo = {};

    if (!sAttr || !dAttr) {
        QAL_ERR(LOG_TAG, "Invalid input paramters");
        goto exit;
    }
    QAL_DBG(LOG_TAG, "Enter. %s", __func__);
    /* get RM instance */
    if (!rm) {
        rm = ResourceManager::getInstance();
        if (!rm) {
            QAL_ERR(LOG_TAG, "ResourceManager getInstance failed");
            goto exit;
        }
    }
    QAL_VERBOSE(LOG_TAG, "get RM instance success and noOfDevices %d \n", noOfDevices);

    mQalDevice = new qal_device [noOfDevices];
    if (!mQalDevice) {
        QAL_ERR(LOG_TAG, "mQalDevice not created");
        goto exit;
    }
    for (int i = 0; i < noOfDevices; i++) {
        if (!rm->isDeviceReady(dAttr[i].id)) {
            QAL_ERR(LOG_TAG, "Device %d is not ready\n", dAttr[i].id);
            continue;  //  continue with other devices for combo usecase
        }

        //TODO: shift this to rm or somewhere else where we can read the supported config from xml
        mQalDevice[count].id = dAttr[i].id;
        if (mQalDevice[count].id == QAL_DEVICE_OUT_USB_DEVICE ||
            mQalDevice[count].id == QAL_DEVICE_OUT_USB_HEADSET ||
            mQalDevice[count].id == QAL_DEVICE_IN_USB_DEVICE ||
            mQalDevice[count].id == QAL_DEVICE_IN_USB_HEADSET) {
            mQalDevice[count].address = dAttr[i].address;
        }
        if (sAttr->direction != QAL_AUDIO_OUTPUT) {
           status = rm->getDeviceInfo(mQalDevice[count].id, sAttr->type, &ecinfo);
           if(status) {
              QAL_ERR(LOG_TAG, "get ec info failed");
           }
        }
        status = rm->getDeviceConfig((struct qal_device *)&mQalDevice[count], sAttr, ecinfo.channels);
        if (status) {
           QAL_ERR(LOG_TAG, "Not able to get Device config %d", status);
           goto exit;
        }
        count++;
    }

    if (!count)
        goto exit;

    if (rm->isStreamSupported(sAttr, mQalDevice, count)) {
        switch (sAttr->type) {
            case QAL_STREAM_LOW_LATENCY:
            case QAL_STREAM_DEEP_BUFFER:
            case QAL_STREAM_GENERIC:
            case QAL_STREAM_VOIP_TX:
            case QAL_STREAM_VOIP_RX:
            case QAL_STREAM_PCM_OFFLOAD:
            case QAL_STREAM_VOICE_CALL:
            case QAL_STREAM_LOOPBACK:
                //TODO:for now keeping QAL_STREAM_PLAYBACK_GENERIC for ULLA need to check
                stream = new StreamPCM(sAttr, mQalDevice, count, modifiers,
                                   noOfModifiers, rm);
                break;
            case QAL_STREAM_COMPRESSED:
                stream = new StreamCompress(sAttr, mQalDevice, count, modifiers,
                                        noOfModifiers, rm);
                break;
            case QAL_STREAM_VOICE_UI:
                stream = new StreamSoundTrigger(sAttr, mQalDevice, count, modifiers,
                                            noOfModifiers, rm);
                break;
            default:
                QAL_ERR(LOG_TAG, "unsupported stream type 0x%x", sAttr->type);
                break;
        }
    } else {
        QAL_ERR(LOG_TAG,"Requested config not supported");
        goto exit;
    }
exit:
    if (stream) {
        QAL_DBG(LOG_TAG, "Exit. stream creation success");
    } else {
        if (mQalDevice)
            delete mQalDevice;
        QAL_ERR(LOG_TAG, "stream creation failed");
    }

    QAL_ERR(LOG_TAG, "stream %pK created", stream);
    return stream;
}

int32_t  Stream::getStreamAttributes(struct qal_stream_attributes *sAttr)
{
    int32_t status = 0;

    if (!sAttr) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid stream attribute pointer, status %d", status);
        goto exit;
    }
    casa_mem_cpy(sAttr, sizeof(qal_stream_attributes), mStreamAttr,
                     sizeof(qal_stream_attributes));
    QAL_DBG(LOG_TAG, "stream_type %d stream_flags %d direction %d",
            sAttr->type, sAttr->flags, sAttr->direction);

exit:
    return status;
}

int32_t  Stream::getModifiers(struct modifier_kv *modifiers,uint32_t *noOfModifiers)
{
    int32_t status = 0;

    if (!mModifiers || !noOfModifiers) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid modifers pointer, status %d", status);
        goto exit;
    }
    casa_mem_cpy (modifiers, sizeof(modifier_kv), mModifiers,
                      sizeof(modifier_kv));
    *noOfModifiers = mNoOfModifiers;
    QAL_DBG(LOG_TAG, "noOfModifiers %u", *noOfModifiers);
exit:
    return status;
}

int32_t Stream::getEffectParameters(void *effect_query, size_t *payload_size)
{
    int32_t status = 0;

    if (!effect_query) {
        QAL_ERR(LOG_TAG, "invalid query");
        return -EINVAL;
    }

    qal_param_payload *qal_param = (qal_param_payload *)effect_query;
    effect_qal_payload_t *effectPayload = (effect_qal_payload_t *)qal_param->effect_payload;
    qal_effect_custom_payload_t *customPayload =
        (qal_effect_custom_payload_t *)effectPayload->payload;
    status = session->getEffectParameters(this, effectPayload);
    if (status) {
       QAL_ERR(LOG_TAG, "getParameters %d failed with %d", customPayload->paramId, status);
    }

    // minus size of (param id)
    *payload_size = effectPayload->payloadSize - sizeof(uint32_t);

    return status;
}

int32_t Stream::getStreamType (qal_stream_type_t* streamType)
{
    int32_t status = 0;

    if (!streamType || !mStreamAttr) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid stream type, status %d", status);
        goto exit;
    }
    *streamType = mStreamAttr->type;
    QAL_DBG(LOG_TAG, "streamType - %d", *streamType);

exit:
    return status;
}

int32_t Stream::getStreamDirection(qal_stream_direction_t *dir)
{
    int32_t status = 0;

    if (!dir || !mStreamAttr) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid inputs, status %d", status);
    } else {
        *dir = mStreamAttr->direction;
        QAL_DBG(LOG_TAG, "stream direction - %d", *dir);
    }
    return status;
}

int32_t  Stream::getAssociatedDevices(std::vector <std::shared_ptr<Device>> &aDevices)
{
    int32_t status = 0;

    QAL_ERR(LOG_TAG, "no. of devices %d", mDevices.size());
    for (int32_t i=0; i < mDevices.size(); i++) {
        aDevices.push_back(mDevices[i]);
    }

exit:
    return status;
}

int32_t  Stream::getAssociatedSession(Session **s)
{
    int32_t status = 0;

    if (!s) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid session, status %d", status);
        goto exit;
    }
    *s = session;
    QAL_DBG(LOG_TAG, "session %pK", *s);
exit:
    return status;
}

int32_t  Stream::getVolumeData(struct qal_volume_data *vData)
{
    int32_t status = 0;

    if (!vData) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid stream attribute pointer, status %d", status);
        goto exit;
    }

    if (mVolumeData != NULL) {
        casa_mem_cpy(vData, sizeof(uint32_t) +
                      (sizeof(struct qal_channel_vol_kv) * (mVolumeData->no_of_volpair)),
                      mVolumeData, sizeof(uint32_t) +
                      (sizeof(struct qal_channel_vol_kv) * (mVolumeData->no_of_volpair)));

        QAL_DBG(LOG_TAG, "num config %x", (mVolumeData->no_of_volpair));
        for(int32_t i=0; i < (mVolumeData->no_of_volpair); i++) {
            QAL_VERBOSE(LOG_TAG, "Volume payload mask:%x vol:%f",
                (mVolumeData->volume_pair[i].channel_mask), (mVolumeData->volume_pair[i].vol));
        }
    } else {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "volume has not been set, status %d", status);
    }
exit:
    return status;
}
int32_t Stream::setBufInfo(size_t *in_buf_size, size_t in_buf_count,
                           size_t *out_buf_size, size_t out_buf_count)
{
    int32_t status = 0;
    int16_t nBlockAlignIn, nBlockAlignOut ;        // block size of data
    struct qal_stream_attributes *sattr = NULL;
    sattr = (struct qal_stream_attributes *)calloc(1, sizeof(struct qal_stream_attributes));
    if (!sattr) {
        status = -ENOMEM;
        QAL_ERR(LOG_TAG, "stream attribute malloc failed %s, status %d", strerror(errno), status);
        goto exit;
    }

    if (!in_buf_size)
        QAL_DBG(LOG_TAG, "%s: In Buffer size %d, In Buffer count %d",
            __func__, *in_buf_size, in_buf_count);

    if (!out_buf_size)
        QAL_DBG(LOG_TAG, "%s: Out Buffer size %d and Out Buffer count %d",
        __func__, *out_buf_size, out_buf_count);
    inBufCount = in_buf_count;
    outBufCount = out_buf_count;

    status = getStreamAttributes(sattr);
    if (sattr->direction == QAL_AUDIO_OUTPUT) {
        if(!out_buf_size) {
            status = -EINVAL;
            QAL_ERR(LOG_TAG, "Invalid output buffer size status %d", status);
            goto exit;
        }
        outBufSize = *out_buf_size;
        nBlockAlignOut = ((sattr->out_media_config.bit_width) / 8) *
                      (sattr->out_media_config.ch_info->channels);
        QAL_ERR(LOG_TAG, "no of buf %d and send buf %x", outBufCount, outBufSize);

        //If the read size is not a multiple of BlockAlign;
        //Make sure we read blockaligned bytes from the file.
        if ((outBufSize % nBlockAlignOut) != 0) {
            outBufSize = ((outBufSize / nBlockAlignOut) * nBlockAlignOut);
        }
        *out_buf_size = outBufSize;

    } else if (sattr->direction == QAL_AUDIO_INPUT) {
        if(!in_buf_size) {
            status = -EINVAL;
            QAL_ERR(LOG_TAG, "Invalid input buffer size status %d", status);
            goto exit;
        }
        inBufSize = *in_buf_size;
        //inBufSize = (sattr->in_media_config.bit_width) * (sattr->in_media_config.ch_info->channels) * 32;
        nBlockAlignIn = ((sattr->in_media_config.bit_width) / 8) *
                      (sattr->in_media_config.ch_info->channels);
        //If the read size is not a multiple of BlockAlign;
        //Make sure we read blockaligned bytes from the file.
        if ((inBufSize % nBlockAlignIn) != 0) {
            inBufSize = ((inBufSize / nBlockAlignIn) * nBlockAlignIn);
        }
        *in_buf_size = inBufSize;
    } else {
        if(!in_buf_size || !out_buf_size) {
            status = -EINVAL;
            QAL_ERR(LOG_TAG, "Invalid buffer size status %d", status);
            goto exit;
        }
        outBufSize = *out_buf_size;
        nBlockAlignOut = ((sattr->out_media_config.bit_width) / 8) *
                      (sattr->out_media_config.ch_info->channels);
        QAL_DBG(LOG_TAG, "no of buf %d and send buf %x", outBufCount, outBufSize);

        //If the read size is not a multiple of BlockAlign;
        //Make sure we read blockaligned bytes from the file.
        if ((outBufSize % nBlockAlignOut) != 0) {
            outBufSize = ((outBufSize / nBlockAlignOut) * nBlockAlignOut);
        }
        *out_buf_size = outBufSize;

        inBufSize = *in_buf_size;
        nBlockAlignIn = ((sattr->in_media_config.bit_width) / 8) *
                      (sattr->in_media_config.ch_info->channels);
        //If the read size is not a multiple of BlockAlign;
        //Make sure we read blockaligned bytes from the file.
        if ((inBufSize % nBlockAlignIn) != 0) {
            inBufSize = ((inBufSize / nBlockAlignIn) * nBlockAlignIn);
        }
        *in_buf_size = inBufSize;
    }
exit:
    return status;
}

int32_t Stream::getBufInfo(size_t *in_buf_size, size_t *in_buf_count,
                           size_t *out_buf_size, size_t *out_buf_count)
{
    int32_t status = 0;

    if (in_buf_size)
        *in_buf_size = inBufSize;
    if (in_buf_count)
        *in_buf_count = inBufCount;
    if (out_buf_size)
        *out_buf_size = outBufSize;
    if (out_buf_count)
        *out_buf_count = outBufCount;

    if (!in_buf_size)
        QAL_DBG(LOG_TAG, "%s: In Buffer size %d, In Buffer count %d",
        __func__, *in_buf_size, in_buf_count);

    if (!out_buf_size)
        QAL_DBG(LOG_TAG, "%s: Out Buffer size %d and Out Buffer count %d",
        __func__, *out_buf_size, out_buf_count);

    return status;
}

bool Stream::isStreamAudioOutFmtSupported(qal_audio_fmt_t format)
{
    switch (format) {
    case QAL_AUDIO_FMT_DEFAULT_PCM:
    case QAL_AUDIO_FMT_MP3:
    case QAL_AUDIO_FMT_AAC:
    case QAL_AUDIO_FMT_AAC_ADTS:
    case QAL_AUDIO_FMT_AAC_ADIF:
    case QAL_AUDIO_FMT_AAC_LATM:
    case QAL_AUDIO_FMT_WMA_STD:
    case QAL_AUDIO_FMT_ALAC:
    case QAL_AUDIO_FMT_APE:
    case QAL_AUDIO_FMT_WMA_PRO:
    case QAL_AUDIO_FMT_FLAC:
    case QAL_AUDIO_FMT_VORBIS:
        return true;
    default:
        return false;
    }
}

int32_t Stream::getTimestamp(struct qal_session_time *stime)
{
    int32_t status = 0;
    if (!stime) {
        status = -EINVAL;
        QAL_ERR(LOG_TAG, "Invalid session time pointer, status %d", status);
        goto exit;
    }
    status = session->getTimestamp(stime);
    if (0 != status) {
        QAL_ERR(LOG_TAG, "Failed to get session timestamp status %d", status);
    }
exit:
    return status;
}

int32_t Stream::disconnectStreamDevice(Stream* streamHandle, qal_device_id_t dev_id)
{
    int32_t status = -EINVAL;
    mStreamMutex.lock();
    status = disconnectStreamDevice_l(streamHandle, dev_id);
    mStreamMutex.unlock();

    return status;
}

int32_t Stream::disconnectStreamDevice_l(Stream* streamHandle, qal_device_id_t dev_id)
{
    int32_t status = -EINVAL;

    // Stream does not know if the same device is being used by other streams or not
    // So if any other streams are using the same device that has to be handled outside of stream
    // resouce manager ??

    for (int i = 0; i < mDevices.size(); i++) {
        if (dev_id == mDevices[i]->getSndDeviceId()) {
            QAL_ERR(LOG_TAG, "device %d name %s, going to stop",
                mDevices[i]->getSndDeviceId(), mDevices[i]->getQALDeviceName().c_str());

            session->disconnectSessionDevice(streamHandle, mStreamAttr->type, mDevices[i]);

            status = mDevices[i]->stop();
            if (0 != status) {
                QAL_ERR(LOG_TAG, "device stop failed with status %d", status);
                goto error_1;
            }
            // Call unblocked version of deregiser as this function is called
            // with resoucemanager lock
            rm->deregisterDevice_l(mDevices[i]);

            status = mDevices[i]->close();
            if (0 != status) {
                QAL_ERR(LOG_TAG, "device close failed with status %d", status);
                goto error_1;
            }
            mDevices.erase(mDevices.begin() + i);
            break;
        }
    }

error_1:
    return status;
}

int32_t Stream::connectStreamDevice(Stream* streamHandle, struct qal_device *dattr)
{
    int32_t status = -EINVAL;
    mStreamMutex.lock();
    status = connectStreamDevice_l(streamHandle, dattr);
    mStreamMutex.unlock();

    return status;
}

int32_t Stream::connectStreamDevice_l(Stream* streamHandle, struct qal_device *dattr)
{
    int32_t status = -EINVAL;
    std::shared_ptr<Device> dev = nullptr;

    if (!dattr) {
        QAL_ERR(LOG_TAG, "invalid params");
        status = -EINVAL;
        goto error_1;
    }

    dev = Device::getInstance(dattr, rm);
    if (!dev) {
        QAL_ERR(LOG_TAG, "Device creation failed");
        goto error_1;
    }

    /* Check if we need to check here or above if bt_Sco is on for sco usecase
     * Device::getInstance will not set device attributes if the device instance
     * created previously so set device config explictly.
     */
    dev->setDeviceAttributes(*dattr);

    QAL_ERR(LOG_TAG, "device %d name %s, going to start",
        dev->getSndDeviceId(), dev->getQALDeviceName().c_str());

    status = dev->open();
    if (0 != status) {
        QAL_ERR(LOG_TAG, "device %d open failed with status %d",
            dev->getSndDeviceId(), status);
        goto error_1;
    }

    mDevices.push_back(dev);
    status = session->setupSessionDevice(streamHandle, mStreamAttr->type, dev);
    if (0 != status) {
        QAL_ERR(LOG_TAG, "setupSessionDevice for %d failed with status %d",
                dev->getSndDeviceId(), status);
        mDevices.pop_back();
        dev->close();
        goto error_1;
    }

    rm->registerDevice_l(dev);

    status = dev->start();
    if (0 != status) {
        QAL_ERR(LOG_TAG, "device %d name %s, start failed with status %d",
            dev->getSndDeviceId(), dev->getQALDeviceName().c_str(), status);
        goto error_2;
    }
    session->connectSessionDevice(streamHandle, mStreamAttr->type, dev);

    goto error_1;

error_2:
    mDevices.pop_back();
    rm->deregisterDevice_l(dev);
    dev->close();
error_1:
    return status;
}

int32_t Stream::switchDevice(Stream* streamHandle, uint32_t numDev, struct qal_device *newDevices)
{
    int32_t status = 0;
    int32_t count = 0;
    bool isNewDeviceA2dp = false;
    bool isCurDeviceA2dp = false;
    uint32_t deviceSlots[QAL_DEVICE_IN_MAX];
    std::vector <std::tuple<Stream *, uint32_t>> streamDevDisconnect, sharedBEStreamDev;
    std::vector <std::tuple<Stream *, struct qal_device *>> StreamDevConnect;

    mStreamMutex.lock();

    if ((numDev == 0) || (!newDevices)) {
        QAL_ERR(LOG_TAG, "invalid param for device switch");
        status = -EINVAL;
        goto done;
    }

    for (int i = 0; i < numDev; i++) {
        if (newDevices[i].id == QAL_DEVICE_OUT_BLUETOOTH_A2DP) {
            isNewDeviceA2dp = true;
            break;
        }
    }

    for (int i = 0; i < mDevices.size(); i++) {
        if (mDevices[i]->getSndDeviceId() == QAL_DEVICE_OUT_BLUETOOTH_A2DP) {
            isCurDeviceA2dp = true;
            break;
        }
    }

    for (int i = 0; i < numDev; i++) {
        /*
         * When A2DP is disconnected the
         * music playback is paused and the policy manager sends routing=0
         * But the audioflinger continues to write data until standby time
         * (3sec). As BT is turned off, the write gets blocked.
         * Avoid this by routing audio to speaker until standby.
         */
        if ((newDevices[i].id == QAL_DEVICE_NONE) &&   /* This assumes that QAL_DEVICE_NODE comes as single device */
            (isCurDeviceA2dp == true) && !rm->isDeviceReady(QAL_DEVICE_OUT_BLUETOOTH_A2DP)) {
            newDevices[i].id = QAL_DEVICE_OUT_SPEAKER;
            rm->getDeviceConfig(&newDevices[i], NULL, 0);
        }

        if (newDevices[i].id == QAL_DEVICE_NONE)
            continue;

        if (!rm->isDeviceReady(newDevices[i].id)) {
            QAL_ERR(LOG_TAG, "Device %d is not ready\n", newDevices[i].id);
        } else {
            deviceSlots[count] = i;
            count++;
        }
    }

    /*  No new device is ready */
    if (count == 0) {
        QAL_ERR(LOG_TAG, "No new device is ready to connect");
        status = -ENODEV;
        goto done;
    }

    if (a2dp_compress_mute && (mStreamAttr->type == QAL_STREAM_COMPRESSED) &&
        !isNewDeviceA2dp) {
        setMute(false);
        a2dp_compress_mute = false;
    }

    QAL_INFO(LOG_TAG, "number of active devices %d, new devices %d", mDevices.size(), count);

    /* created stream device disconnect list */
    streamDevDisconnect.clear();
    for (int i = 0; i < count; i++) {
        sharedBEStreamDev.clear();
        // get active stream device pairs sharing the same backend with new devices.
        rm->getSharedBEActiveStreamDevs(sharedBEStreamDev, newDevices[deviceSlots[i]].id);
        if (sharedBEStreamDev.size() <= 0) {
            for (const auto &device : mDevices) {
                // check to make sure device direction is the same
                if (rm->matchDevDir(device->getSndDeviceId(), newDevices[deviceSlots[i]].id))
                    streamDevDisconnect.push_back({streamHandle, device->getSndDeviceId()});
            }
        } else {
            for (const auto &elem : sharedBEStreamDev) {
                streamDevDisconnect.push_back(elem);
            }
        }
    }

    /* created stream device connect list */
    StreamDevConnect.clear();
    for (int i = 0; i < count; i++) {
        if (streamDevDisconnect.size() == 0) {
            StreamDevConnect.push_back({streamHandle, &newDevices[deviceSlots[i]]});
        }
        for (const auto &elem : streamDevDisconnect) {
            // check to make sure device direction is the same
            if (rm->matchDevDir(newDevices[deviceSlots[i]].id, std::get<1>(elem)))
                StreamDevConnect.push_back({std::get<0>(elem), &newDevices[deviceSlots[i]]});
        }
    }

    /* remove duplicates */
    std::sort(streamDevDisconnect.begin(), streamDevDisconnect.end());
    streamDevDisconnect.erase(
            std::unique(streamDevDisconnect.begin(), streamDevDisconnect.end()),
            streamDevDisconnect.end());
    std::sort(StreamDevConnect.begin(), StreamDevConnect.end());
    StreamDevConnect.erase(
            std::unique(StreamDevConnect.begin(), StreamDevConnect.end()),
            StreamDevConnect.end());

    QAL_DBG(LOG_TAG, "disconnectList size is %d, connectList size is %d",
            streamDevDisconnect.size(), StreamDevConnect.size());
    for (const auto &elem : streamDevDisconnect)
        QAL_DBG(LOG_TAG, "disconnectList: stream handler 0x%p, device id %d",
                std::get<0>(elem), std::get<1>(elem));
    for (const auto &elem : StreamDevConnect)
        QAL_DBG(LOG_TAG, "connectList: stream handler 0x%p, device id %d",
                std::get<0>(elem), std::get<1>(elem)->id);

    status = rm->streamDevSwitch(streamDevDisconnect, StreamDevConnect);
    if (status) {
        QAL_ERR(LOG_TAG, "Device switch failed");
    }

done:
    mStreamMutex.unlock();
    return status;
}

bool Stream::isGKVMatch(qal_key_vector_t* gkv)
{
    int i = 0;
    uint32_t key = 0;
    uint32_t value = 0;
    int status = 0;
    PayloadBuilder* builder = new PayloadBuilder();
    bool match = false;

    if (!gkv) {
        QAL_ERR(LOG_TAG, "GKV is empty");
        return false;
    }

    for (i = 0; i < gkv->num_tkvs; i++) {
        if (gkv->kvp[i].key == STREAMRX || gkv->kvp[i].key == STREAMTX) {
            std::vector <std::pair<int, int>> kvpair_stream;

            status = builder->populateStreamKV(this, kvpair_stream);
            if (status) {
                QAL_ERR(LOG_TAG, "failed to populate streamkv");
                goto err;
            } else {
                if (gkv->kvp[i].value == kvpair_stream[0].second)
                    match = true;
                else
                    match = false;
            }
        } else if (gkv->kvp[i].key == DEVICERX || gkv->kvp[i].key == DEVICETX) {
            for (int k = 0; k < mDevices.size(); k++) {
                std::vector <std::pair<int, int>> kvpair_device;
                status = builder->populateDeviceKV(this,
                            mDevices[k]->getSndDeviceId(), kvpair_device);
                if (status) {
                    goto err;
                } else {
                    if (gkv->kvp[i].value == kvpair_device[0].second) {
                        match = true;
                        break;
                    }
                    else
                        match = false;
                }
            }
        }
    }

err:
    delete builder;
    return match;
}

