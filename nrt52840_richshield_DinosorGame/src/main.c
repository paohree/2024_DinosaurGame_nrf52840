#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

int number_led_matrix_arr_4x8[10][32]= {
    { // 0
        0,1,1,0,
        1,0,0,1,
        1,0,0,1,
        1,0,0,1,
        1,0,0,1,
        1,0,0,1,
        1,0,0,1,
        0,1,1,0
    },
    { // 1
        0,0,1,0,
        0,1,1,0,
        0,0,1,0,
        0,0,1,0,
        0,0,1,0,
        0,0,1,0,
        0,0,1,0,
        0,1,1,1
    },
    { // 2
        0,1,1,0,
        1,0,0,1,
        0,0,0,1,
        0,0,1,0,
        0,1,0,0,
        1,0,0,0,
        1,0,0,0,
        1,1,1,1
    },
    { // 3
        1,1,1,0,
        0,0,0,1,
        0,0,0,1,
        0,1,1,0,
        0,0,0,1,
        0,0,0,1,
        0,0,0,1,
        1,1,1,0
    },
    { // 4
        0,0,1,0,
        0,1,1,0,
        1,0,1,0,
        1,0,1,0,
        1,1,1,1,
        0,0,1,0,
        0,0,1,0,
        0,0,1,0
    },
    { // 5
        1,1,1,1,
        1,0,0,0,
        1,0,0,0,
        1,1,1,0,
        0,0,0,1,
        0,0,0,1,
        0,0,0,1,
        1,1,1,0
    },
    { // 6
        0,1,1,0,
        1,0,0,0,
        1,0,0,0,
        1,1,1,0,
        1,0,0,1,
        1,0,0,1,
        1,0,0,1,
        0,1,1,0
    },
    { // 7
        1,1,1,1,
        0,0,0,1,
        0,0,0,1,
        0,0,1,0,
        0,1,0,0,
        0,1,0,0,
        0,1,0,0,
        0,1,0,0
    },
    { // 8
        0,1,1,0,
        1,0,0,1,
        1,0,0,1,
        0,1,1,0,
        1,0,0,1,
        1,0,0,1,
        1,0,0,1,
        0,1,1,0
    },
    { // 9
        0,1,1,0,
        1,0,0,1,
        1,0,0,1,
        0,1,1,1,
        0,0,0,1,
        0,0,0,1,
        0,0,0,1,
        1,1,1,0
    }
};

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define LED_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(holtek_ht16k33)

const struct device *led; // 전역 변수 선언

#define GPIO_FAIL -1
#define GPIO_OK 0

static struct gpio_callback button0_cb_data;
static struct gpio_callback button_reset_cb_data;

#define GROUND_START 96
#define GROUND_END 128

#define DINO_LED 84
#define LIVES 4
int lives = LIVES;

int score;

bool is_jumping = false;
int dino_pos = DINO_LED;
int previous_dino_pos = DINO_LED; // 추가된 변수
int obstacle_pos = GROUND_END - 1;

bool should_turn_off_led = false; // 추가된 변수
int led_to_turn_off = 0; // 추가된 변수


#define BUTTON_NODE DT_ALIAS(sw0)
#define RESET_NODE DT_ALIAS(sw1)

static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static const struct gpio_dt_spec button_reset = GPIO_DT_SPEC_GET(RESET_NODE, gpios);


//Using for rotaryencoder
struct sensor_value val;
int rc;
const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(qdec0));


static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios)
};

void game_loop(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(game_work, game_loop);

void button0_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (!is_jumping) {
        should_turn_off_led = true; // LED를 꺼야 함을 표시
        led_to_turn_off = dino_pos; // 꺼야 할 LED의 위치 설정
        is_jumping = true;
        dino_pos -= 16; // Jump up one row
        printk("Dino jumps to %d\n", dino_pos);
    }
}

void button_reset_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    reset_game();
}

void reset_game()
{
    lives = LIVES;
    dino_pos = DINO_LED;
    previous_dino_pos = DINO_LED;
    obstacle_pos = GROUND_END - 1;
    is_jumping = false;
    should_turn_off_led = false;
    score = 0;

    for (int i = 0; i < LIVES; i++) {
        gpio_pin_set_dt(&leds[i], 1);
    }

    printk("Game reset\n");

    
    k_work_reschedule(&game_work, K_NO_WAIT);
}

int gpio_init(void)
{
    int err = GPIO_FAIL;

    // Set button0 interrupt
    printk("Setting button0 interrupt\n");

    err = gpio_is_ready_dt(&button0);
    if (!err) {
        printk("Error gpio_is_ready_dt button0 pin %d\n", err);
        return GPIO_FAIL;
    }

    err = gpio_pin_configure_dt(&button0, GPIO_INPUT | GPIO_PULL_UP);
    if (err < 0) {
        printk("Error configuring button0 pin %d\n", err);
        return GPIO_FAIL;
    }

    err = gpio_pin_interrupt_configure_dt(&button0, GPIO_INT_EDGE_TO_ACTIVE);
    if (err != 0) {
        printk("Error configuring interrupt on button0 pin %d\n", err);
        return GPIO_FAIL;
    }
    gpio_init_callback(&button0_cb_data, button0_callback, BIT(button0.pin));
    gpio_add_callback(button0.port, &button0_cb_data);

    // Set reset button interrupt
    printk("Setting reset button interrupt\n");

    err = gpio_is_ready_dt(&button_reset);
    if (!err) {
        printk("Error gpio_is_ready_dt button_reset pin %d\n", err);
        return GPIO_FAIL;
    }

    err = gpio_pin_configure_dt(&button_reset, GPIO_INPUT | GPIO_PULL_UP);
    if (err < 0) {
        printk("Error configuring button_reset pin %d\n", err);
        return GPIO_FAIL;
    }

    err = gpio_pin_interrupt_configure_dt(&button_reset, GPIO_INT_EDGE_TO_ACTIVE);
    if (err != 0) {
        printk("Error configuring interrupt on button_reset pin %d\n", err);
        return GPIO_FAIL;
    }
    gpio_init_callback(&button_reset_cb_data, button_reset_callback, BIT(button_reset.pin));
    gpio_add_callback(button_reset.port, &button_reset_cb_data);

    // Configure LEDs
    for (int i = 0; i < LIVES; i++) {
        if (!device_is_ready(leds[i].port)) {
            printk("Error: LED device %d not ready\n", i);
            return GPIO_FAIL;
        }
        err = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_ACTIVE);
        if (err < 0) {
            printk("Error %d: failed to configure LED %d pin\n", err, i);
            return GPIO_FAIL;
        }
    }

    return GPIO_OK;
}

void game_loop(struct k_work *work)
{
    static int jump_counter = 0;
    static int game_speed = 121; // Game speed in milliseconds
    static int loop_counter = 0; // 루프 반복 횟수를 세는 변수
    const int speed_up_interval = 50; // 속도를 증가시킬 간격 (루프 횟수)
    const int min_game_speed = 20; // 최소 게임 속도 (milliseconds)
    const int max_game_speed = 500; // 최소 게임 속도 (milliseconds)


    // Handle jumping logic
    if (is_jumping) {
        jump_counter++;
        led_off(led, dino_pos);
        if (jump_counter >= 5) { // Adjust jump duration for slower game speed
            dino_pos += 16; // Fall back down
            is_jumping = false;
            jump_counter = 0;
            printk("Dino falls to %d\n", dino_pos);
        }
    }

    // Turn off LED if needed
    if (should_turn_off_led) {
        led_off(led, led_to_turn_off);
        should_turn_off_led = false;
    }

    // Turn off the previous position LED when falling down
    if (!is_jumping && previous_dino_pos != dino_pos) {
        led_off(led, previous_dino_pos);
        previous_dino_pos = dino_pos;
    }

    // Set new Dino position
    led_on(led, dino_pos);
    
    // Ensure ground LEDs are always on
    for (int i = GROUND_START; i < GROUND_END; i++) {
        led_on(led, i);
    }

    // Move obstacle
    if (obstacle_pos >= 80) {
        led_off(led, obstacle_pos);
        obstacle_pos--;
        if (obstacle_pos < 80) {
            obstacle_pos = 96 - 1; // Reset obstacle to the end
        } else {
            led_on(led, obstacle_pos);
        }
    }

    // Check for collision
    if (dino_pos == obstacle_pos) {
        printk("Collision detected at %d!\n", dino_pos);
        lives--;
        for (int i = 0; i < 128; i++) {
            led_on(led, i);
            k_sleep(K_MSEC(10));
        }
        for (int i = 128; i >= 0; i--) {
            led_off(led, i);
            k_sleep(K_MSEC(10));
        }
        if (lives > 0) {
            gpio_pin_set_dt(&leds[lives], 0);
            obstacle_pos = GROUND_END - 1; // Reset obstacle to the end
        } else {
            // Game over
            for (int i = 0; i < 128; i++) {
                led_on(led, i);
            }
            k_sleep(K_MSEC(75));
            for (int i = 128; i >= 0; i--) {
                led_off(led, i);
            }
            k_sleep(K_MSEC(75));

            if(score > 9999)
                score = 9999;
            printk("score %d\n",score);
            int thousands=0, hundreds=0, tens=0, ones=0;

            // Extracting digits
            thousands = score / 1000;
            hundreds = (score % 1000) / 100;
            tens = (score % 100) / 10;
            ones = score % 10;

            int num_arr_idx = 0;

            for(int i = 0; i < 128; i+=16){
                for(int j = i; j < (i+4); j++){
                    if(number_led_matrix_arr_4x8[thousands][num_arr_idx] == 1){
                        // printk("[tens] led_on: j:[%d] num_array_idx[%d]\n", j, num_arr_idx);
                        led_on(led, j);
                    } else {
                        led_off(led, j);
                    }

                    num_arr_idx++;
                }
            }

            num_arr_idx = 0;

            for(int i = 0; i < 128; i+=16){
                for(int j = (i+4); j < (i+8); j++){
                    if(number_led_matrix_arr_4x8[hundreds][num_arr_idx] == 1){
                        // printk("[units] led_on: j:[%d] num_array_idx[%d]\n", j, num_arr_idx);
                        led_on(led, j);
                    } else {
                        led_off(led, j);
                    }
                    num_arr_idx++;
                }
            }
        

            num_arr_idx = 0;

            for(int i = 0; i < 128; i+=16){
                for(int j = (i+8); j < (i+12); j++){
                    if(number_led_matrix_arr_4x8[tens][num_arr_idx] == 1){
                        // printk("[tens] led_on: j:[%d] num_array_idx[%d]\n", j, num_arr_idx);
                        led_on(led, j);
                    } else {
                        led_off(led, j);
                    }

                    num_arr_idx++;
                }
            }

            num_arr_idx = 0;

            for(int i = 0; i < 128; i+=16){
                for(int j = (i+12); j < (i+16); j++){
                    if(number_led_matrix_arr_4x8[ones][num_arr_idx] == 1){
                        // printk("[units] led_on: j:[%d] num_array_idx[%d]\n", j, num_arr_idx);
                        led_on(led, j);
                    } else {
                        led_off(led, j);
                    }
                    num_arr_idx++;
                }
            }


            for (int i = 0; i < LIVES; i++) {
                gpio_pin_set_dt(&leds[i], 0);
            }
            k_sleep(K_MSEC(75));
            // for (int i = 128; i >= 0; i--) {
            //     led_off(led, i);
            // }
            // k_sleep(K_MSEC(75));
            // printk("Game over\n");
            // for (int i = 0; i < LIVES; i++) {
            //     gpio_pin_set_dt(&leds[i], 0);
            // }
            
            return;
        }
    }

    // 일정 간격마다 게임 속도 증가
    // loop_counter++;
    rc = sensor_sample_fetch(dev);
    if (rc != 0) {
        printk("Failed to fetch sample (%d)\n", rc);
        return;
    }

    rc = sensor_channel_get(dev, SENSOR_CHAN_ROTATION, &val);
    if (rc != 0) {
        printk("Failed to get data (%d)\n", rc);
        return;
    }
    
    loop_counter++;
    if (val.val1 != 0) {
        
        game_speed -= (val.val1 / 18) * 5; // 속도를 줄임 (게임 속도 증가)
        if(game_speed < min_game_speed){
            game_speed = min_game_speed;
        }
        else if(game_speed > max_game_speed){
            game_speed = max_game_speed;
        }
        printk("encoder_val : %d\n",val.val1);
        printk("Increasing game speed: %d ms\n", game_speed);
    }
    else if (loop_counter % speed_up_interval == 0 && game_speed > min_game_speed) {
        game_speed -= 5; // 속도를 줄임 (게임 속도 증가)
        printk("Increasing game speed: %d ms\n", game_speed);
    }



    // Schedule next iteration
    score++;
    k_work_reschedule(&game_work, K_MSEC(game_speed));
}

void main(void)
{
    int err;

    err = gpio_init();
    if (err != GPIO_OK) {
        printk("Error gpio_init %d\n", err);
        return;
    }

    led = DEVICE_DT_GET(LED_NODE);
    if (!device_is_ready(led)) {
        LOG_ERR("LED device not ready");
        return;
    }

    printk("LED device ready\n");

    led_set_brightness(led, 0, 0.1);

    for (int i = GROUND_START; i < GROUND_END; i++) {
        led_on(led, i);
    }

    // Set initial Dino position
    led_on(led, dino_pos);

    // Initialize lives LEDs
    for (int i = 0; i < LIVES; i++) {
        gpio_pin_set_dt(&leds[i], 1);
    }

    //check for rotaryencoder
    if (!device_is_ready(dev)) {
		printk("Qdec device is not ready\n");
		return;
	}

    // Start game loop
    k_work_schedule(&game_work, K_NO_WAIT);
}
