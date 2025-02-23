/* sound output via OSS */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>

#include "sndout_oss.h"

static int sounddev = -1, mixerdev = -1;
static int can_write_safe;


int sndout_oss_init(void)
{
	if (mixerdev >= 0) close(mixerdev);
  	mixerdev = open("/dev/mixer", O_RDWR);
	if (mixerdev == -1)
	{
		perror("open(\"/dev/mixer\")");
	}

	return 0;
}

int sndout_oss_start(int rate, int frame_samples, int stereo)
{
	static int s_oldrate = 0, s_old_fsamples = 0, s_oldstereo = 0;
	int frag, bsize, bits, ret;

	// if no settings change, we don't need to do anything,
	// since audio is never stopped
	if (rate == s_oldrate && s_old_fsamples == frame_samples && s_oldstereo == stereo)
		return 0;

	if (sounddev >= 0) {
		ioctl(sounddev, SOUND_PCM_SYNC, 0);
		close(sounddev);
	}

	sounddev = open("/dev/dsp", O_WRONLY|O_ASYNC);
	if (sounddev == -1)
	{
		perror("open(\"/dev/dsp\")");
		return -1;
	}

	// calculate buffer size. We one to fit 1 frame worth of sound data.
	// Also ignore mono because both GP2X and Wiz mixes mono to stereo anyway.
	bsize = frame_samples << 2;

	for (frag = 0; bsize; bsize >>= 1, frag++)
		;

	frag |= 16 << 16;	// fragment count
	ret = ioctl(sounddev, SNDCTL_DSP_SETFRAGMENT, &frag);
	if (ret < 0)
		perror("SNDCTL_DSP_SETFRAGMENT failed");

	bits = 16;
	ret = ioctl(sounddev, SNDCTL_DSP_STEREO, &stereo);
	if (ret == 0)
		ret = ioctl(sounddev, SNDCTL_DSP_SETFMT, &bits);
	if (ret == 0)
		ret = ioctl(sounddev, SNDCTL_DSP_SPEED, &rate);
	if (ret < 0)
		perror("failed to set audio format");

	// not sure if this is still needed (avoiding driver bugs?)
	usleep(192*1024);

	printf("sndout_oss_start: %d/%dbit/%s, %d buffers of %i bytes\n",
		rate, bits, stereo ? "stereo" : "mono", frag >> 16, 1 << (frag & 0xffff));

	s_oldrate = rate; s_old_fsamples = frame_samples; s_oldstereo = stereo;
	can_write_safe = 0;
	return 0;
}

int sndout_oss_write(const void *buff, int len)
{
	return write(sounddev, buff, len);
}

int sndout_oss_can_write(int bytes)
{
	audio_buf_info bi;
	int ret;

#ifdef __GP2X__
	// note: SNDCTL_DSP_GETOSPACE crashes F100 kernel for some reason
	// if called too early, so we work around here
	if (can_write_safe++ < 8)
		return 1;
#endif
	ret = ioctl(sounddev, SNDCTL_DSP_GETOSPACE, &bi);
	if (ret < 0)
		return 1;

	// have enough bytes to write + 4 extra frags
	return bi.bytes - bi.fragsize * 4 >= bytes ? 1 : 0;
}

void sndout_oss_sync(void)
{
	ioctl(sounddev, SOUND_PCM_SYNC, 0);
}

void sndout_oss_setvol(int l, int r)
{
	if (mixerdev < 0) return;

 	l=l<0?0:l; l=l>255?255:l; r=r<0?0:r; r=r>255?255:r;
 	l<<=8; l|=r;
 	ioctl(mixerdev, SOUND_MIXER_WRITE_PCM, &l); /*SOUND_MIXER_WRITE_VOLUME*/
}

void sndout_oss_exit(void)
{
	if (sounddev >= 0) close(sounddev); sounddev = -1;
	if (mixerdev >= 0) close(mixerdev); mixerdev = -1;
}

