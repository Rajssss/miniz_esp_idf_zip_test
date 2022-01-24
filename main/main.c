#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "miniz.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "sdkconfig.h"

#ifndef CONFIG_ESP32_SPIRAM_SUPPORT
#pragma message("This utility will not work without PSRAM")
#endif


#define	MANUAL_FORMAT_PARTITION


static const char *TAG = "miniz_zip_test";



static esp_vfs_littlefs_conf_t littlefs_conf = {
		  .base_path = "/littlefs",
		  .partition_label = "storage",
		  .dont_mount = false,
		  .format_if_mount_failed = true
};


static const char *s_pTest_str =
  "MISSION CONTROL I wouldn't worry too much about the computer. First of all, there is still a chance that he is right, despite your tests, and";

static const char *s_pComment = "This is a comment";

static char file_name[50];



static void fs_init(void)
{
    size_t total = 0, used = 0;

	ESP_LOGI(TAG, "fs_init: Initializing LittleFS");

	esp_err_t ret = esp_vfs_littlefs_register(&littlefs_conf);
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL)
		{
			ESP_LOGE(TAG, "fs_init: Failed to mount or format filesystem");
		}
		else if (ret == ESP_ERR_NOT_FOUND)
		{
			ESP_LOGE(TAG, "fs_init: Failed to find LittleFS partition");
		}
		else
		{
			ESP_LOGE(TAG, "fs_init: Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
		}
	}

#ifdef MANUAL_FORMAT_PARTITION
	ret = esp_littlefs_format("storage");
	if(ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Can't format iso7168 partiton");
	}
#endif
	ret = esp_littlefs_info(littlefs_conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "fs_init: Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "fs_init: Partition size: total: %d, used: %d", total, used);
    }
}


static void zip_test(void)
{
	int i, sort_iter;
	mz_bool status;
	size_t uncomp_size;
	mz_zip_archive zip_archive;
	void *p;
	const int N = 1;
	char data[2048];
	char archive_filename[64];
	static const char *s_Test_archive_filename = "/littlefs/mz_example2_test.zip";

	assert((strlen(s_pTest_str) + 64) < sizeof(data));

	ESP_LOGI(TAG, "miniz.c version: %s\n", MZ_VERSION);

	// Delete the test archive, so it doesn't keep growing as we run this test
	remove(s_Test_archive_filename);

	// Append a bunch of text files to the test archive
	for (i = (N - 1); i >= 0; --i)
	{
		sprintf(archive_filename, "%u.txt", i);
		sprintf(data, "%u %s %u", (N - 1) - i, s_pTest_str, i);

		// Add a new file to the archive. Note this is an IN-PLACE operation, so if it fails your archive is probably hosed (its central directory may not be complete) but it should be recoverable using zip -F or -FF. So use caution with this guy.
		// A more robust way to add a file to an archive would be to read it into memory, perform the operation, then write a new archive out to a temp file and then delete/rename the files.
		// Or, write a new archive to disk to a temp file, then delete/rename the files. For this test this API is fine.
		status = mz_zip_add_mem_to_archive_file_in_place(
				s_Test_archive_filename, archive_filename,
				data, strlen(data) + 1, s_pComment,
				(uint16_t)strlen(s_pComment), MZ_BEST_COMPRESSION);

		if (!status)
		{
			ESP_LOGE(TAG, "mz_zip_add_mem_to_archive_file_in_place failed!: %d\n", status);
		}
	}

	// Add a directory entry for testing
	status = mz_zip_add_mem_to_archive_file_in_place(s_Test_archive_filename, "directory/", NULL, 0, "no comment", (uint16_t)strlen("no comment"), MZ_BEST_COMPRESSION);
	if (!status)
	{
		ESP_LOGE(TAG, "mz_zip_add_mem_to_archive_file_in_place failed!\n");
	}

	// Now try to open the archive.
	memset(&zip_archive, 0, sizeof(zip_archive));

	status = mz_zip_reader_init_file(&zip_archive, s_Test_archive_filename, 0);
	if (!status)
	{
		ESP_LOGE(TAG, "mz_zip_reader_init_file() failed!\n");
	}

	// Get and print information about each file in the archive.
	for (i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); i++)
	{
		mz_zip_archive_file_stat file_stat;
		if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat))
		{
			ESP_LOGE(TAG, "mz_zip_reader_file_stat() failed!\n");
			mz_zip_reader_end(&zip_archive);
		}

		ESP_LOGI(TAG, "Filename: \"%s\", Comment: \"%s\", Uncompressed size: %u, Compressed size: %u, Is Dir: %u\n", file_stat.m_filename, file_stat.m_comment, (uint)file_stat.m_uncomp_size, (uint)file_stat.m_comp_size, mz_zip_reader_is_file_a_directory(&zip_archive, i));

		if (!strcmp(file_stat.m_filename, "directory/"))
		{
			if (!mz_zip_reader_is_file_a_directory(&zip_archive, i))
			{
				ESP_LOGE(TAG, "mz_zip_reader_is_file_a_directory() didn't return the expected results!\n");
				mz_zip_reader_end(&zip_archive);
			}
		}
	}

	// Close the archive, freeing any resources it was using
	mz_zip_reader_end(&zip_archive);

	// Now verify the compressed data
	for (sort_iter = 0; sort_iter < 2; sort_iter++)
	{
		memset(&zip_archive, 0, sizeof(zip_archive));
		status = mz_zip_reader_init_file(&zip_archive, s_Test_archive_filename, sort_iter ? MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY : 0);
		if (!status)
		{
			ESP_LOGE(TAG, "mz_zip_reader_init_file() failed!\n");
		}

		for (i = 0; i < N; i++)
		{
			sprintf(archive_filename, "%u.txt", i);
			sprintf(data, "%u %s %u", (N - 1) - i, s_pTest_str, i);

			// Try to extract all the files to the heap.
			p = mz_zip_reader_extract_file_to_heap(&zip_archive, archive_filename, &uncomp_size, 0);
			if (!p)
			{
				ESP_LOGE(TAG, "mz_zip_reader_extract_file_to_heap() failed!\n");
				mz_zip_reader_end(&zip_archive);
			}

			// Make sure the extraction really succeeded.
			if ((uncomp_size != (strlen(data) + 1)) || (memcmp(p, data, strlen(data))))
			{
				ESP_LOGE(TAG, "mz_zip_reader_extract_file_to_heap() failed to extract the proper data\n");
				mz_free(p);
				mz_zip_reader_end(&zip_archive);
			}

			ESP_LOGI(TAG, "Successfully extracted file \"%s\", size %u\n", archive_filename, (uint)uncomp_size);
			ESP_LOGI(TAG, "File data: \"%s\"\n", (const char *)p);

			// We're done.
			mz_free(p);
		}

		// Close the archive, freeing any resources it was using
		mz_zip_reader_end(&zip_archive);
	}

	ESP_LOGI(TAG, "Success.\n");



	FILE *f = fopen(s_Test_archive_filename, "rb");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		fclose(f);
	}

	fseek(f, 0, SEEK_END);
	uint32_t file_size = ftell(f);
	rewind(f);

	unsigned char *file_buffer = (unsigned char*)malloc((file_size * sizeof(unsigned char)) + 1);
	if(file_buffer == NULL)
		ESP_LOGE(TAG, "aes_crypto_encrypt: file_buffer: not enough memory");

	int ret = fread(file_buffer, sizeof(unsigned char), file_size, f);
	if (file_size != ret)
	{
		ESP_LOGE(TAG, "Can't read the file properly");
	}
	fclose(f);




	while(1)
	{

	}

}



void app_main(void)
{
	fs_init();

//	zip_test();

    xTaskCreatePinnedToCore(zip_test, "zip_test", 1024*20, NULL, 2, NULL, 1);

	while(true)
    {
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
}

