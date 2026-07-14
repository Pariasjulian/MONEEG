#include <errno.h>
#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "bacn_gpio.h"

static struct gpiod_chip *chip = NULL;
static struct gpiod_line_request *drdy_request = NULL;
static struct gpiod_line_request *start_request = NULL;

static struct gpiod_line_request *
request_output_line(struct gpiod_chip *chip, unsigned int offset,
		    enum gpiod_line_value value, const char *consumer)
{
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *request = NULL;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;
	int ret;

	settings = gpiod_line_settings_new();
	if (!settings)
		return NULL;

	gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_settings_set_output_value(settings, value);

	line_cfg = gpiod_line_config_new();
	if (!line_cfg) {
		gpiod_line_settings_free(settings);
		return NULL;
	}

	ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
	if (ret) {
		gpiod_line_config_free(line_cfg);
		gpiod_line_settings_free(settings);
		return NULL;
	}

	if (consumer) {
		req_cfg = gpiod_request_config_new();
		if (req_cfg) {
			gpiod_request_config_set_consumer(req_cfg, consumer);
		}
	}

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

	if (req_cfg) gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	gpiod_line_settings_free(settings);

	return request;
}

static struct gpiod_line_request *
request_input_line(struct gpiod_chip *chip, unsigned int offset, const char *consumer)
{
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *request = NULL;
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;
	int ret;

	settings = gpiod_line_settings_new();
	if (!settings)
		return NULL;

	gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);

	line_cfg = gpiod_line_config_new();
	if (!line_cfg) {
		gpiod_line_settings_free(settings);
		return NULL;
	}

	ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
	if (ret) {
		gpiod_line_config_free(line_cfg);
		gpiod_line_settings_free(settings);
		return NULL;
	}

	if (consumer) {
		req_cfg = gpiod_request_config_new();
		if (req_cfg) {
			gpiod_request_config_set_consumer(req_cfg, consumer);
		}
	}

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

	if (req_cfg) gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	gpiod_line_settings_free(settings);

	return request;
}

int gpio_init(void)
{
    const char *chip_path = "/dev/gpiochip0";
    chip = gpiod_chip_open(chip_path);
    if (!chip) {
        perror("gpiod_chip_open failed");
        return -1;
    }

    drdy_request = request_input_line(chip, READY_PIN, "status-DRDY");
    if (!drdy_request) {
        fprintf(stderr, "failed to request DRDY line\n");
        gpiod_chip_close(chip);
        return -1;
    }

    start_request = request_output_line(chip, START_PIN, GPIOD_LINE_VALUE_ACTIVE, "start-GPIO");
    if (!start_request) {
        fprintf(stderr, "failed to request START line\n");
        gpiod_line_request_release(drdy_request);
        gpiod_chip_close(chip);
        return -1;
    }

    printf("[GPIO] Initialized persistent requests\n");
    return 0;
}

void gpio_cleanup(void)
{
    if (start_request) {
        gpiod_line_request_release(start_request);
        start_request = NULL;
    }
    if (drdy_request) {
        gpiod_line_request_release(drdy_request);
        drdy_request = NULL;
    }
    if (chip) {
        gpiod_chip_close(chip);
        chip = NULL;
    }
    printf("[GPIO] Cleanup complete\n");
}

uint8_t status_DRDY(void)
{
    if (!drdy_request) return 1;
    return gpiod_line_request_get_value(drdy_request, READY_PIN);
}

uint8_t Start_INI(void)
{
    if (!start_request) return EXIT_FAILURE;
    printf("[GPIO] Set start pin HIGH\n");
    gpiod_line_request_set_value(start_request, START_PIN, GPIOD_LINE_VALUE_ACTIVE);
    return EXIT_SUCCESS;
}

uint8_t Start_ADQ(void)
{
    if (!start_request) return EXIT_FAILURE;
    printf("[GPIO] Pulse start pin for ADQ\n");
    gpiod_line_request_set_value(start_request, START_PIN, GPIOD_LINE_VALUE_INACTIVE);
    usleep(1000); // 1ms pulse
    gpiod_line_request_set_value(start_request, START_PIN, GPIOD_LINE_VALUE_ACTIVE);
    return EXIT_SUCCESS;
}
