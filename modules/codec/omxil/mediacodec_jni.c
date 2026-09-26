/*****************************************************************************
 * mediacodec_jni.c: mc_api implementation using JNI
 *****************************************************************************
 * Copyright © 2015 VLC authors and VideoLAN, VideoLabs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <jni.h>
#include <stdint.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_threads.h>

#include <OMX_Core.h>
#include <OMX_Component.h>
#include "omxil_utils.h"

#include "mediacodec.h"
#include "mediacodec_profile.h"
#include "mediacodec_candidates.h"
#include "../../video_output/android/env.h"

#define THREAD_NAME "mediacodec"

/*****************************************************************************
 * JNI Initialisation
 *****************************************************************************/

struct jfields
{
    jclass media_codec_list_class;
    jmethodID get_codec_count, get_codec_info_at, is_encoder, get_capabilities_for_type;
    jmethodID is_feature_supported;
    jfieldID profile_levels_field, profile_field, level_field;
    jmethodID get_supported_types, get_name;
};
static struct jfields jfields;

enum Types
{
    METHOD, STATIC_METHOD, FIELD
};

#define OFF(x) offsetof(struct jfields, x)
struct classname
{
    const char *name;
    int offset;
};
static const struct classname classes[] = {
    { "android/media/MediaCodecList", OFF(media_codec_list_class) },
    { NULL, 0 },
};

struct member
{
    const char *name;
    const char *sig;
    const char *class;
    int offset;
    int type;
    bool critical;
};
static const struct member members[] = {
    { "getCodecCount", "()I", "android/media/MediaCodecList", OFF(get_codec_count), STATIC_METHOD, true },
    { "getCodecInfoAt", "(I)Landroid/media/MediaCodecInfo;", "android/media/MediaCodecList", OFF(get_codec_info_at), STATIC_METHOD, true },

    { "isEncoder", "()Z", "android/media/MediaCodecInfo", OFF(is_encoder), METHOD, true },
    { "getSupportedTypes", "()[Ljava/lang/String;", "android/media/MediaCodecInfo", OFF(get_supported_types), METHOD, true },
    { "getName", "()Ljava/lang/String;", "android/media/MediaCodecInfo", OFF(get_name), METHOD, true },
    { "getCapabilitiesForType", "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;", "android/media/MediaCodecInfo", OFF(get_capabilities_for_type), METHOD, true },
    { "isFeatureSupported", "(Ljava/lang/String;)Z", "android/media/MediaCodecInfo$CodecCapabilities", OFF(is_feature_supported), METHOD, false },
    { "profileLevels", "[Landroid/media/MediaCodecInfo$CodecProfileLevel;", "android/media/MediaCodecInfo$CodecCapabilities", OFF(profile_levels_field), FIELD, true },
    { "profile", "I", "android/media/MediaCodecInfo$CodecProfileLevel", OFF(profile_field), FIELD, true },
    { "level", "I", "android/media/MediaCodecInfo$CodecProfileLevel", OFF(level_field), FIELD, true },


    { NULL, NULL, NULL, 0, 0, false },
};

static int jstrcmp(JNIEnv* env, jobject str, const char* str2)
{
    jsize len = (*env)->GetStringUTFLength(env, str);
    if ((*env)->ExceptionCheck(env))
        return -2;
    if (len != (jsize) strlen(str2))
        return -1;
    const char *ptr = (*env)->GetStringUTFChars(env, str, NULL);
    if (ptr == NULL)
        return -2;
    int ret = memcmp(ptr, str2, len);
    (*env)->ReleaseStringUTFChars(env, str, ptr);
    return ret == 0 ? 0 : -1;
}

static inline bool check_exception(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return true;
    }
    else
        return false;
}
#define CHECK_EXCEPTION() check_exception(env)

static inline jstring jni_new_string(JNIEnv *env, const char *psz_string)
{
    jstring jstring = (*env)->NewStringUTF(env, psz_string);
    return !CHECK_EXCEPTION() ? jstring : NULL;
}
#define JNI_NEW_STRING(psz_string) jni_new_string(env, psz_string)


/* Initialize all jni fields.
 * Done only one time during the first initialisation */
static bool
InitJNIFields (vlc_object_t *p_obj, JNIEnv *env)
{
    static vlc_mutex_t lock = VLC_STATIC_MUTEX;
    static int i_init_state = -1;
    bool ret;

    vlc_mutex_lock(&lock);

    if (i_init_state != -1)
        goto end;

    i_init_state = 0;

    for (int i = 0; classes[i].name; i++)
    {
        jclass clazz = (*env)->FindClass(env, classes[i].name);
        if (CHECK_EXCEPTION())
        {
            msg_Warn(p_obj, "Unable to find class %s", classes[i].name);
            goto end;
        }
        *(jclass*)((uint8_t*)&jfields + classes[i].offset) =
            (jclass) (*env)->NewGlobalRef(env, clazz);
        (*env)->DeleteLocalRef(env, clazz);
    }

    jclass last_class = NULL;
    for (int i = 0; members[i].name; i++)
    {
        if (i == 0 || strcmp(members[i].class, members[i - 1].class))
        {
            if (last_class != NULL)
                (*env)->DeleteLocalRef(env, last_class);
            last_class = (*env)->FindClass(env, members[i].class);
        }

        if (CHECK_EXCEPTION())
        {
            msg_Warn(p_obj, "Unable to find class %s", members[i].class);
            goto end;
        }

        switch (members[i].type) {
        case METHOD:
            *(jmethodID*)((uint8_t*)&jfields + members[i].offset) =
                (*env)->GetMethodID(env, last_class, members[i].name, members[i].sig);
            break;
        case STATIC_METHOD:
            *(jmethodID*)((uint8_t*)&jfields + members[i].offset) =
                (*env)->GetStaticMethodID(env, last_class, members[i].name, members[i].sig);
            break;
        case FIELD:
            *(jfieldID*)((uint8_t*)&jfields + members[i].offset) =
                (*env)->GetFieldID(env, last_class, members[i].name, members[i].sig);
            break;
        }
        if (CHECK_EXCEPTION())
        {
            msg_Warn(p_obj, "Unable to find the member %s in %s",
                     members[i].name, members[i].class);
            if (members[i].critical)
                goto end;
        }
    }
    if (last_class != NULL)
        (*env)->DeleteLocalRef(env, last_class);

    i_init_state = 1;
end:
    ret = i_init_state == 1;
    if (!ret)
        msg_Err(p_obj, "MediaCodec jni init failed");

    vlc_mutex_unlock(&lock);
    return ret;
}

static char *GetManufacturer(JNIEnv *env)
{
    char *manufacturer = NULL;

    jclass clazz = (*env)->FindClass(env, "android/os/Build");
    if (CHECK_EXCEPTION() || clazz == NULL)
        return NULL;

    jfieldID id = (*env)->GetStaticFieldID(env, clazz, "MANUFACTURER",
                                           "Ljava/lang/String;");
    if (CHECK_EXCEPTION())
        goto end;

    jstring jstr = (*env)->GetStaticObjectField(env, clazz, id);

    if (CHECK_EXCEPTION() || jstr == NULL)
        goto end;

    const char *str = (*env)->GetStringUTFChars(env, jstr, 0);
    if (str)
    {
        manufacturer = strdup(str);
        (*env)->ReleaseStringUTFChars(env, jstr, str);
    }
    else
        CHECK_EXCEPTION();
    (*env)->DeleteLocalRef(env, jstr);

end:
    (*env)->DeleteLocalRef(env, clazz);
    return manufacturer;
}

struct mc_enumeration
{
    vlc_object_t *obj;
    JNIEnv *env;
    vlc_fourcc_t codec;
    bool video;
    const struct mc_score_rule *rules;
    const char *failed;
    const char *ignore_names;
};

static int EnumerateCandidates(void *opaque,
                               const struct mc_candidate_request *request,
                               struct mc_candidates *candidates)
{
    const struct mc_enumeration *ctx = opaque;
    vlc_object_t *p_obj = ctx->obj;
    JNIEnv *env = ctx->env;
    int ret = VLC_SUCCESS;
    jstring jmime = JNI_NEW_STRING(request->mime);
    if (jmime == NULL)
        return VLC_ENOMEM;

    int num_codecs = (*env)->CallStaticIntMethod(env,
                         jfields.media_codec_list_class, jfields.get_codec_count);
    if (CHECK_EXCEPTION())
    {
        ret = VLC_EGENERIC;
        goto done;
    }

    for (int i = 0; i < num_codecs; i++)
    {
        jobject codec_capabilities = NULL, profile_levels = NULL;
        jobject info = NULL, name = NULL, types = NULL;
        const char *name_ptr = NULL;
        bool found = false, b_adaptive = false;
        int quirks = 0;

        info = (*env)->CallStaticObjectMethod(env, jfields.media_codec_list_class,
                                              jfields.get_codec_info_at, i);
        if (CHECK_EXCEPTION() || info == NULL)
            goto error;
        name = (*env)->CallObjectMethod(env, info, jfields.get_name);
        if (CHECK_EXCEPTION() || name == NULL)
            goto error;
        jsize name_len = (*env)->GetStringUTFLength(env, name);
        if (CHECK_EXCEPTION())
            goto error;
        name_ptr = (*env)->GetStringUTFChars(env, name, NULL);
        if (CHECK_EXCEPTION() || name_ptr == NULL)
        {
            ret = VLC_ENOMEM;
            goto loopclean;
        }
        if (strpbrk(name_ptr, "\t\n") != NULL ||
            OMXCodec_IsBlacklisted(name_ptr, name_len) ||
            mc_candidate_failed(ctx->failed, request->mime, name_ptr) ||
            mc_candidate_score(ctx->rules, name_ptr) < 0)
            goto loopclean;

        bool encoder = (*env)->CallBooleanMethod(env, info, jfields.is_encoder);
        if (CHECK_EXCEPTION())
            goto error;
        if (encoder)
            goto loopclean;

        /* Check declared MIME support before querying capabilities: asking for
         * an unsupported type normally throws IllegalArgumentException. */
        types = (*env)->CallObjectMethod(env, info, jfields.get_supported_types);
        if (CHECK_EXCEPTION() || types == NULL)
            goto error;
        int num_types = (*env)->GetArrayLength(env, types);
        if (CHECK_EXCEPTION())
            goto error;
        bool supports_mime = false;
        bool supports_required = request->required_mime == NULL;
        for (int j = 0; j < num_types; j++)
        {
            jobject type = (*env)->GetObjectArrayElement(env, types, j);
            if (CHECK_EXCEPTION() || type == NULL)
            {
                if (type != NULL)
                    (*env)->DeleteLocalRef(env, type);
                goto error;
            }
            int match = jstrcmp(env, type, request->mime);
            bool exception = CHECK_EXCEPTION() || match == -2;
            supports_mime |= match == 0;
            if (!exception && request->required_mime != NULL)
            {
                match = jstrcmp(env, type, request->required_mime);
                exception = CHECK_EXCEPTION() || match == -2;
                supports_required |= match == 0;
            }
            (*env)->DeleteLocalRef(env, type);
            if (exception)
                goto error;
        }
        if (!supports_mime || !supports_required)
            goto loopclean;

        codec_capabilities = (*env)->CallObjectMethod(env, info,
                                  jfields.get_capabilities_for_type, jmime);
        if (CHECK_EXCEPTION() || codec_capabilities == NULL)
            goto error;
        profile_levels = (*env)->GetObjectField(env, codec_capabilities,
                                               jfields.profile_levels_field);
        if (CHECK_EXCEPTION())
            goto error;
        int profile_levels_len = profile_levels != NULL
                               ? (*env)->GetArrayLength(env, profile_levels) : 0;
        if (CHECK_EXCEPTION())
            goto error;
        if (jfields.is_feature_supported)
        {
            jstring jfeature = JNI_NEW_STRING("adaptive-playback");
            if (jfeature == NULL)
            {
                ret = VLC_ENOMEM;
                goto loopclean;
            }
            b_adaptive = (*env)->CallBooleanMethod(env, codec_capabilities,
                                         jfields.is_feature_supported, jfeature);
            bool exception = CHECK_EXCEPTION();
            (*env)->DeleteLocalRef(env, jfeature);
            if (exception)
                goto error;
        }
        bool ignore_profile = MediaCodec_MatchDecoderList(
                       ctx->ignore_names, name_ptr, name_len, false);
        found = request->profile <= 0 || ignore_profile;
        /* This component does not expose profiles but supports high profile. */
        if (!strncmp(name_ptr, "OMX.LUMEVideoDecoder", __MIN(20, name_len)))
            found = true;
        for (int j = 0; j < profile_levels_len && !found; j++)
        {
            jobject level = (*env)->GetObjectArrayElement(env, profile_levels, j);
            if (CHECK_EXCEPTION() || level == NULL)
            {
                if (level != NULL)
                    (*env)->DeleteLocalRef(env, level);
                goto error;
            }
            int omx_profile = (*env)->GetIntField(env, level, jfields.profile_field);
            bool exception = CHECK_EXCEPTION();
            (*env)->DeleteLocalRef(env, level);
            if (exception)
                goto error;
            found = convert_omx_to_profile_idc(ctx->codec, omx_profile)
                    == request->profile;
        }
        if (found)
        {
            /* Amazon MTK components report the Surface size, not video size.
             * Disable adaptive mode so the bitstream parser supplies the size. */
            bool ignore_size = false;
            static const char mtk_dec[] = "OMX.MTK.VIDEO.DECODER.";
            if (strncmp(name_ptr, mtk_dec, sizeof(mtk_dec) - 1) == 0)
            {
                char *manufacturer = GetManufacturer(env);
                if (manufacturer == NULL)
                    goto error;
                if (strcmp(manufacturer, "Amazon") == 0)
                    ignore_size = true;
                free(manufacturer);
            }
            if (ignore_size)
                quirks |= MC_API_VIDEO_QUIRKS_IGNORE_SIZE;
            else if (b_adaptive)
                quirks |= MC_API_VIDEO_QUIRKS_ADAPTIVE;
            if (mc_candidates_offer(candidates, request, ctx->rules, ctx->failed,
                                    name_ptr, false, quirks) != 0)
                ret = VLC_ENOMEM;
            else
                msg_Dbg(p_obj, "MediaCodec candidate: %s score=%"PRId64" mime=%s profile=%d",
                        name_ptr, (int64_t)mc_candidate_score(ctx->rules, name_ptr)
                                  + request->bonus, request->mime, request->profile);
        }
        goto loopclean;
error:
        msg_Warn(p_obj, "MediaCodec enumeration failed for %s", request->mime);
        ret = VLC_EGENERIC;
loopclean:
        if (name_ptr != NULL)
            (*env)->ReleaseStringUTFChars(env, name, name_ptr);
        if (name)
            (*env)->DeleteLocalRef(env, name);
        if (profile_levels)
            (*env)->DeleteLocalRef(env, profile_levels);
        if (types)
            (*env)->DeleteLocalRef(env, types);
        if (codec_capabilities)
            (*env)->DeleteLocalRef(env, codec_capabilities);
        if (info)
            (*env)->DeleteLocalRef(env, info);
        if (ret != VLC_SUCCESS || (found && !ctx->video))
            break;
    }
done:
    (*env)->DeleteLocalRef(env, jmime);
    return ret;
}

int MediaCodec_SelectCandidate(vlc_object_t *p_obj, vlc_fourcc_t codec,
                              const struct mc_candidate_request *requests,
                              size_t count, bool video, bool allow_relax,
                              struct mc_candidate *result)
{
    *result = (struct mc_candidate) { 0 };
    JNIEnv *env = android_getEnv(p_obj, THREAD_NAME);
    if (env == NULL || !InitJNIFields(p_obj, env))
        return VLC_EGENERIC;

    struct mc_score_rule *rules = NULL;
    char *failed = NULL, *ignore_names = NULL;
    int ret;
    if (video)
    {
        char *scores = var_InheritString(p_obj, "decoder-score-list");
        unsigned invalid;
        int parsed = mc_score_rules_parse(scores, &rules, &invalid);
        free(scores);
        if (parsed != 0)
        {
            ret = VLC_ENOMEM;
            goto done;
        }
        if (invalid != 0)
            msg_Warn(p_obj, "Ignoring %u invalid decoder score rules", invalid);
        if (var_Type(p_obj, "mediacodec-failed-candidates") != 0)
        {
            failed = var_GetString(p_obj, "mediacodec-failed-candidates");
            if (failed == NULL)
            {
                ret = VLC_ENOMEM;
                goto done;
            }
        }
    }
    ignore_names = var_InheritString(p_obj, "decoder-ignore-profile");
    struct mc_enumeration ctx = {
        p_obj, env, codec, video, rules, failed, ignore_names,
    };
    bool relaxed;
    ret = mc_candidates_select(requests, count, video && allow_relax,
                               EnumerateCandidates, &ctx, result, &relaxed);
    if (relaxed)
        msg_Warn(p_obj, "MediaCodec: no strict profile candidate, retried without profile filtering");
    if (ret == 1)
        ret = VLC_ENOENT;
    else if (ret == VLC_SUCCESS)
        msg_Dbg(p_obj, "MediaCodec selected %s score=%"PRId64" mime=%s",
                result->name, result->score, result->mime);
done:
    mc_score_rules_clear(rules);
    free(failed);
    free(ignore_names);
    return ret;
}
