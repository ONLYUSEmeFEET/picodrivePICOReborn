#ifndef _CD_FILE_H
#define _CD_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#define TYPE_ISO 1
#define TYPE_BIN 2
#define TYPE_MP3 3
#define TYPE_WAV 4

typedef enum
{
	CIT_NOT_CD = 0,
	CIT_ISO,
	CIT_BIN,
	CIT_CUE
}
cd_img_type;


PICO_INTERNAL int  Load_CD_Image(const char *iso_name, cd_img_type type);
PICO_INTERNAL void Unload_ISO(void);
PICO_INTERNAL int  FILE_Read_One_LBA_CDC(void);


#ifdef __cplusplus
};
#endif

#endif
