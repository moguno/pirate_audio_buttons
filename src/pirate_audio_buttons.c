#include <wiringPi.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <linux/uinput.h>


// プロトタイプ宣言
void push_button_a();
void push_button_b();
void push_button_x();
void push_button_y();

//! プログラム終了フラグ
static int g_need_finish = 0;

//! イベントコード
static int g_event_codes[4];

//! /dev/uinputのファイルディスクリプタ
static int g_uinput_fd;

//! A,B,X,YボタンのGPIOピン
static int g_gpio_pins[4] = {5, 6, 16, 20};

//! A,B,X,Yボタンの割り込みハンドラ
static void (*g_gpio_handlers[4])() = {&push_button_a, &push_button_b, &push_button_x, &push_button_y};

/**
 * @fn
 * @brief SIGINTをハンドルする関数
 */
void sigint_handler(int signum) {
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
 * @param[in] event_code イベントコード
 */
void push_button(int event_code) {
	struct input_event ev;
	int ret;

	if (event_code == 0) {
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

}

/**
 * @fn
 * @brief Aボタンを押したときの処理
 */
void push_button_a() {
	push_button(g_event_codes[0]);
}

/**
 * @fn
 * @brief Bボタンを押したときの処理
 */
void push_button_b() {
	push_button(g_event_codes[1]);
}

/**
 * @fn
 * @brief Xボタンを押したときの処理
 */
void push_button_x() {
	push_button(g_event_codes[2]);
}

/**
 * @fn
 * @brief Yボタンを押したときの処理
 */
void push_button_y() {
	push_button(g_event_codes[3]);
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
		wiringPiISR(g_gpio_pins[i], INT_EDGE_FALLING, g_gpio_handlers[i]);
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

	signal(SIGINT, &sigint_handler);

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
