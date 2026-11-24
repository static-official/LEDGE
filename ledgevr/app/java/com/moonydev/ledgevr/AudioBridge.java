package com.moonydev.ledgevr;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.media.SoundPool;

/**
 * LEDGE 1.1 audio bridge — background music playlist (MediaPlayer looping
 * through assets/music/*.ogg) + SFX (SoundPool from assets/sfx/*.ogg).
 * All static; native calls in via JNI. Every entry point catches exceptions
 * so a missing asset can never crash the game.
 */
public class AudioBridge {
    // SFX ids — MUST match the native SFX_* order in main.cpp/main_mobile.cpp
    private static final String[] SFX_FILES = {
            "grab", "release", "land", "goal", "bounce", "place", "delete",
            "click", "open", "success", "whoosh", "pop", "emote"
    };
    // music playlist order (track index used by native musicPlay())
    private static final String[] MUSIC_FILES = {
            "menu_candy_skies", "build_ledge_loops", "game_pastel_rush",
            "game_cloud_hop", "game_neon_climb", "game_bubble_bass",
            "credits_star_splash"
    };
    public static final String[] MUSIC_TITLES = {
            "Candy Skies (menu)", "Ledge Loops (build)", "Pastel Rush",
            "Cloud Hop", "Neon Climb", "Bubble Bass", "Star Splash"
    };

    private static Context ctx = null;
    private static SoundPool sp = null;
    private static int[] sfxIds = new int[SFX_FILES.length];
    private static MediaPlayer mp = null;
    private static int track = 0;
    private static float musicVol = 0.8f, sfxVol = 1.0f;
    private static boolean sfxLoaded = false;

    public static void init(Context c) {
        if (ctx != null) return;      // idempotent
        ctx = c.getApplicationContext();
        try {
            AudioAttributes attrs = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build();
            sp = new SoundPool.Builder().setMaxStreams(6).setAudioAttributes(attrs).build();
            for (int i = 0; i < SFX_FILES.length; i++) sfxIds[i] = -1;
            for (int i = 0; i < SFX_FILES.length; i++) {
                try {
                    AssetFileDescriptor afd = ctx.getAssets().openFd("sfx/" + SFX_FILES[i] + ".ogg");
                    sfxIds[i] = sp.load(afd, 1);
                    afd.close();
                } catch (Exception e) {
                    sfxIds[i] = -1;
                }
            }
            sfxLoaded = true;
        } catch (Exception e) {
            sp = null;
        }
    }

    public static void playSfx(int idx) {
        if (sp == null || !sfxLoaded || idx < 0 || idx >= sfxIds.length) return;
        try {
            if (sfxIds[idx] > 0) sp.play(sfxIds[idx], sfxVol, sfxVol, 1, 0, 1f);
        } catch (Exception ignored) { }
    }

    public static void playMusic(int idx) {
        if (ctx == null) return;
        try {
            track = ((idx % MUSIC_FILES.length) + MUSIC_FILES.length) % MUSIC_FILES.length;
            stopMusic();
            mp = new MediaPlayer();
            AssetFileDescriptor afd = ctx.getAssets().openFd("music/" + MUSIC_FILES[track] + ".ogg");
            mp.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
            afd.close();
            mp.setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());
            mp.setLooping(false);
            mp.setVolume(musicVol, musicVol);
            mp.setOnCompletionListener(new MediaPlayer.OnCompletionListener() {
                @Override public void onCompletion(MediaPlayer m) { nextTrack(); }
            });
            mp.prepare();
            mp.start();
        } catch (Exception e) {
            mp = null;
        }
    }

    public static void nextTrack() { playMusic(track + 1); }

    public static void stopMusic() {
        try {
            if (mp != null) {
                mp.stop();
                mp.release();
            }
        } catch (Exception ignored) { }
        mp = null;
    }

    public static void setMusicVol(float v) {
        musicVol = Math.max(0f, Math.min(1f, v));
        try {
            if (mp != null) mp.setVolume(musicVol, musicVol);
        } catch (Exception ignored) { }
    }

    public static void setSfxVol(float v) {
        sfxVol = Math.max(0f, Math.min(1f, v));
    }

    public static int getTrack() { return track; }
}
