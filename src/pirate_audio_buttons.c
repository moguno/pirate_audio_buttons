#include <wiringPi.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <linux/uinput.h>

// プロトタイプ宣言
void press_button_a();
void press_button_b();
void press_button_x();
void press_button_y();

//! プログラム終了フラグ
static int g_need_finish = 0;

//! イベントコード
static int g_event_codes[4];

//! /dev/uinputのファイルディスクリプタ
static int g_uinput_fd;

//! A,B,X,YボタンのGPIOピン
static int g_gpio_pins[4] = {5, 6, 16, 20};

//! A,B,X,Yボタンを押したときの割り込みハンドラ
static void (*g_button_press_handlers[4])() = {&press_button_a, &press_button_b, &press_button_x, &press_button_y};

// チャタリング防止用の割り込み無視時間(ミリ秒)
static const int ignore_millisec = 300;

//! 最後にA,B,X,Yボタンが押されてた時間
static struct timespec g_button_last_pressed_times[4] = {0, 0, 0, 0};

/**
 * @fn
 * @brief 終了シグナルをハンドルする関数
 */
void terminate_signal_handler(int signum) {
	g_need_finish = 1;	
}

/**
 * @fn
 * @brief イベントを送信する
 * @param[in] type イベントタイプ
 * @param[in] code イベントコード
 * @param[in] value 値
 * @return int 0:正常、0以外:エラー
 */
int event_send(int type, int code, int value) {
	struct input_event ev;
	int ret;

	memset(&ev, 0, sizeof(ev));

	ev.type = type;
	ev.code = code;
	ev.value = value;

	ret = write(g_uinput_fd, &ev, sizeof(ev));

	if (ret == -1) {
		return -1;
	}

	return 0;
}

/**
 * @fn
 * @brief ボタンを押したときの処理
 * @param[in] index 0:A、1:B、2:X、3:Y
 */
void press_button(int index) {
	struct input_event ev;
	struct timespec ts;
	long long time_nano_last;
	long long time_nano_now;

	int ret;

	int event_code = g_event_codes[index];

	if (event_code == 0) {
		return;
	}

	clock_gettime(CLOCK_REALTIME, &ts);

	time_nano_last = (long long)g_button_last_pressed_times[index].tv_sec * 1000 + (long long)g_button_last_pressed_times[index].tv_nsec / 1000000;
	time_nano_now = (long long)ts.tv_sec * 1000 + (long long)ts.tv_nsec / 1000000;

	if ((time_nano_now - time_nano_last) < ignore_millisec) {
		return;
	}

	ret = event_send(EV_KEY, event_code, 1);

	if (ret != 0) {
		fprintf(stderr, "key press event write error\n");
		return;
	}

	ret = event_send(EV_KEY, event_code, 0);

	if (ret != 0) {
		fprintf(stderr, "key release event write error\n");
		return;
	}

	ret = event_send(EV_SYN, 0, 0);

	if (ret != 0) {
		fprintf(stderr, "sync event write error\n");
		return;
	}

	memcpy(&g_button_last_pressed_times[index], &ts, sizeof(ts));
}

/**
 * @fn
 * @brief Aボタンを押したときの処理
 */
void press_button_a() {
	press_button(0);
}

/**
 * @fn
 * @brief Bボタンを押したときの処理
 */
void press_button_b() {
	press_button(1);
}

/**
 * @fn
 * @brief Xボタンを押したときの処理
 */
void press_button_x() {
	press_button(2);
}

/**
 * @fn
 * @brief Yボタンを押したときの処理
 */
void press_button_y() {
	press_button(3);
}

/**
 * @fn
 * @brief イベントコードを登録する
 * @param[in] fd /dev/uinputのファイルディスクリプタ
 * @param[in] event_codeキーボードイベントコード
 */
int register_event(int fd, int event_code) {
	return ioctl(fd, UI_SET_KEYBIT, event_code);
}

/**
 * @fn
 * @brief GPIO初期化処理
 * @return int 0:正常、0以外:エラー
 */
int setup_gpio() {
	int i;

	wiringPiSetupGpio();

	for (i = 0; i < sizeof(g_gpio_pins) / sizeof(g_gpio_pins[0]); i++) {
		pinMode(g_gpio_pins[i] ,INPUT);
        	pullUpDnControl(g_gpio_pins[i], PUD_UP);
		wiringPiISR(g_gpio_pins[i], INT_EDGE_FALLING, g_button_press_handlers[i]);
	}

	return 0;
}

/**
 * @fn
 * @brief /dev/uinput初期化処理
 * @return int 0:正常、0以外:エラー
 */
int setup_uinput(int *event_codes) {
	int i;
	int fd;

	fd = open("/dev/uinput", O_WRONLY);

	if (fd == -1) {
		fprintf(stderr, "/dev/uinput open error\n");

		return -1;
	}

	ioctl(fd, UI_SET_EVBIT, EV_KEY);

	for (i = 0; i < 4; i++) {
		register_event(fd, event_codes[i]);
	}

	struct uinput_user_dev uidev;

	memset(&uidev, 0, sizeof(uidev));

	strcpy(uidev.name, "test");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x01;
	uidev.id.product = 0x01;
	uidev.id.version = 0x01;

	write(fd, &uidev, sizeof(uidev));

	ioctl(fd, UI_DEV_CREATE, NULL);

	return fd;
}

/**
 * @fn
 * @brief /dev/uinput終了時処理
 * @return int 0:正常、0以外:エラー
 */
int cleanup_uinput(int fd) {
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);

	return 0;
}

/**
 * @fn
 * @brief コマンドの使い方を標準エラーに出力する
 */
void usage(char *program_name) {
	fprintf(stderr, "usage: %s event_code_a event_code_b event_code_x event_code_y\n", program_name);    
}

/**
 * @fn
 * @brief メイン
 * @param[in] argc コマンドライン引数の数
 * @param[in] argv コマンドライン引数
 * @return int 0:正常、0以外:エラー
 */
int main(int argc, char *argv[]) {
	int ret;
	int fd;
	int i;	

	if (argc != 5) {
		usage(argv[0]);
		return 1;
	}

	for(i = 0; i < 4; i++) {
		ret = sscanf(argv[i + 1], "%d", &g_event_codes[i]);

		if ((ret != 1) || (g_event_codes[i] < 0)) {
			fprintf(stderr, "argument %d error\n", i + 1);
			return 1;
		}
	}

	signal(SIGINT, &terminate_signal_handler);
	signal(SIGTERM, &terminate_signal_handler);

	setup_gpio();

	g_uinput_fd = setup_uinput(g_event_codes);

	while(1) {
		if (g_need_finish) {
			break;
		}

		sleep(1);
	}

	return 0;
}
