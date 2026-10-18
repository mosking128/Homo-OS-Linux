#include "ios_calculator.h"
#include "framework/ui/fonts.h"

#include "framework/ui/ios_layout.h"
#include "framework/ui/ios_status_bar.h"

#include "libs/tiny_ttf/lv_tiny_ttf.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOS_ASSET(name) "A:assets/ios/" name
#define CALC_KEY_SIZE 58
#define CALC_KEY_STEP_X 70
#define CALC_KEY_STEP_Y 64
#define CALC_KEY_X 266
#define CALC_KEY_Y (IOS_APP_CONTENT_SAFE_TOP + 68)
#define SCI_KEY_WIDTH 54
#define SCI_KEY_HEIGHT 40
#define SCI_KEY_STEP_X 62
#define SCI_KEY_STEP_Y 45
#define SCI_KEY_X 38
#define SCI_KEY_Y 128
#define CALC_PI 3.14159265358979323846

typedef struct {
    char key;
} calculator_key_t;

static lv_obj_t * calculator_root;
static lv_obj_t * basic_key_panel;
static lv_obj_t * display_panel;
static lv_obj_t * expression_label;
static lv_obj_t * display_label;
static lv_obj_t * clear_label;
static lv_obj_t * mode_label;
static lv_obj_t * second_label;
static lv_obj_t * angle_label;
static lv_obj_t * second_button;
static lv_obj_t * angle_button;
static lv_obj_t * operator_buttons[5];
static lv_obj_t * operator_labels[5];
static lv_obj_t * scientific_buttons[24];
static lv_obj_t * scientific_trig_labels[6];
static const lv_font_t * calculator_display_font;
static const lv_font_t * calculator_key_font;
static calculator_key_t key_data[48];
static uint8_t key_data_count;
static uint8_t scientific_button_count;

static char entry[64] = "0";
static double accumulator;
static double last_left_operand;
static double last_operand;
static char pending_operator;
static char last_operator;
static bool waiting_for_operand = true;
static bool just_evaluated;
static bool has_last_operation;
static bool calculator_error;
static bool scientific_mode;
static bool second_mode;
static bool radians_mode;

static const char * operator_text(char op)
{
    switch(op) {
        case '+': return "+";
        case '-': return "−";
        case '*': return "×";
        case '/': return "÷";
        case '^': return "^";
        default: return "";
    }
}

static void format_number(double value, char * buffer, size_t size)
{
    if(fabs(value) < 1e-12) value = 0.0;
    snprintf(buffer, size, "%.10g", value);
}

static bool calculate(double left, char op, double right, double * result)
{
    switch(op) {
        case '+': *result = left + right; break;
        case '-': *result = left - right; break;
        case '*': *result = left * right; break;
        case '/':
            if(right == 0.0) return false;
            *result = left / right;
            break;
        case '^': *result = pow(left, right); break;
        default: return false;
    }
    return isfinite(*result);
}

static void update_clear_label(void)
{
    bool all_clear = !pending_operator && strcmp(entry, "0") == 0 && !just_evaluated;
    lv_label_set_text(clear_label, all_clear ? "AC" : "C");
}

static void update_operator_styles(void)
{
    static const char operators[] = {'/', '*', '-', '+', '^'};
    for(uint32_t i = 0; i < 5; i++) {
        bool selected = pending_operator == operators[i] && waiting_for_operand;
        lv_obj_set_style_bg_color(operator_buttons[i],
                                  selected ? lv_color_white() : lv_color_hex(0xff9f0a),
                                  LV_PART_MAIN);
        lv_obj_set_style_text_color(operator_labels[i],
                                    selected ? lv_color_hex(0xff9f0a) : lv_color_white(),
                                    LV_PART_MAIN);
    }
}

static void update_display(void)
{
    char expression[96] = "";
    if(calculator_error) {
        lv_label_set_text(display_label, "Error");
        lv_label_set_text(expression_label, "");
    }
    else {
        lv_label_set_text(display_label, entry);
        size_t length = strlen(entry);
        const lv_font_t * font = length > 10 ? &lv_font_montserrat_20 : calculator_display_font;
        lv_obj_set_style_text_font(display_label, font, LV_PART_MAIN);

        if(pending_operator) {
            char first[64];
            bool has_current = !waiting_for_operand;
            format_number(accumulator, first, sizeof(first));
            snprintf(expression, sizeof(expression), "%.30s %s%s%.30s", first,
                     operator_text(pending_operator),
                     has_current ? " " : "", has_current ? entry : "");
        }
        else if(just_evaluated && has_last_operation) {
            char left[64];
            char right[64];
            format_number(last_left_operand, left, sizeof(left));
            format_number(last_operand, right, sizeof(right));
            snprintf(expression, sizeof(expression), "%.30s %s %.30s =", left,
                     operator_text(last_operator), right);
        }
        lv_label_set_text(expression_label, expression);
    }
    update_clear_label();
    update_operator_styles();
}

static void reset_calculator(void)
{
    snprintf(entry, sizeof(entry), "0");
    accumulator = 0.0;
    last_left_operand = 0.0;
    pending_operator = 0;
    last_operator = 0;
    last_operand = 0.0;
    waiting_for_operand = true;
    just_evaluated = false;
    has_last_operation = false;
    calculator_error = false;
    update_display();
}

static void input_digit(char digit)
{
    if(calculator_error) reset_calculator();
    if(just_evaluated && !pending_operator) reset_calculator();

    if(waiting_for_operand) {
        snprintf(entry, sizeof(entry), "%c", digit);
        waiting_for_operand = false;
    }
    else if(strcmp(entry, "0") == 0) {
        snprintf(entry, sizeof(entry), "%c", digit);
    }
    else if(strcmp(entry, "-0") == 0) {
        snprintf(entry, sizeof(entry), "-%c", digit);
    }
    else if(strlen(entry) < 15) {
        size_t length = strlen(entry);
        entry[length] = digit;
        entry[length + 1] = '\0';
    }
    just_evaluated = false;
    update_display();
}

static void input_decimal(void)
{
    if(calculator_error) reset_calculator();
    if(just_evaluated && !pending_operator) reset_calculator();
    if(waiting_for_operand) {
        snprintf(entry, sizeof(entry), "0.");
        waiting_for_operand = false;
    }
    else if(strchr(entry, '.') == NULL) {
        size_t length = strlen(entry);
        if(length < sizeof(entry) - 1) {
            entry[length] = '.';
            entry[length + 1] = '\0';
        }
    }
    just_evaluated = false;
    update_display();
}

static void clear_entry(void)
{
    if(calculator_error) {
        reset_calculator();
        return;
    }
    snprintf(entry, sizeof(entry), "0");
    waiting_for_operand = pending_operator != 0;
    just_evaluated = false;
    update_display();
}

static void input_operator(char op)
{
    if(calculator_error) reset_calculator();
    double current = strtod(entry, NULL);
    if(pending_operator && !waiting_for_operand) {
        double result;
        if(!calculate(accumulator, pending_operator, current, &result)) {
            calculator_error = true;
            pending_operator = 0;
            update_display();
            return;
        }
        accumulator = result;
        format_number(result, entry, sizeof(entry));
    }
    else if(!pending_operator) {
        accumulator = current;
    }
    pending_operator = op;
    waiting_for_operand = true;
    just_evaluated = false;
    update_display();
}

static void input_percent(void)
{
    if(calculator_error) reset_calculator();
    double value = strtod(entry, NULL) / 100.0;
    format_number(value, entry, sizeof(entry));
    waiting_for_operand = false;
    just_evaluated = false;
    update_display();
}

static void input_sign(void)
{
    if(calculator_error) reset_calculator();
    if(waiting_for_operand) {
        snprintf(entry, sizeof(entry), "-0");
        waiting_for_operand = false;
    }
    else {
        double value = -strtod(entry, NULL);
        format_number(value, entry, sizeof(entry));
    }
    just_evaluated = false;
    update_display();
}

static void input_constant(double value)
{
    if(calculator_error) reset_calculator();
    if(just_evaluated && !pending_operator) reset_calculator();
    format_number(value, entry, sizeof(entry));
    waiting_for_operand = false;
    just_evaluated = false;
    update_display();
}

static void input_unary(char op)
{
    if(calculator_error) reset_calculator();
    double value = strtod(entry, NULL);
    double result = value;
    double radians = radians_mode ? value : value * CALC_PI / 180.0;

    switch(op) {
        case 's': result = second_mode ? asin(value) : sin(radians); if(second_mode && !radians_mode) result *= 180.0 / CALC_PI; break;
        case 'c': result = second_mode ? acos(value) : cos(radians); if(second_mode && !radians_mode) result *= 180.0 / CALC_PI; break;
        case 't': result = second_mode ? atan(value) : tan(radians); if(second_mode && !radians_mode) result *= 180.0 / CALC_PI; break;
        case 'u': result = second_mode ? asinh(value) : sinh(value); break;
        case 'v': result = second_mode ? acosh(value) : cosh(value); break;
        case 'w': result = second_mode ? atanh(value) : tanh(value); break;
        case 'n': if(value <= 0.0) calculator_error = true; else result = log(value); break;
        case 'l': if(value <= 0.0) calculator_error = true; else result = log10(value); break;
        case 'r': if(value < 0.0) calculator_error = true; else result = sqrt(value); break;
        case 'q': result = value * value; break;
        case 'i': if(value == 0.0) calculator_error = true; else result = 1.0 / value; break;
        case 'e': result = exp(value); break;
        case 'E': result = pow(10.0, value); break;
        case '!':
            if(value < 0.0 || value > 170.0 || floor(value) != value) calculator_error = true;
            else {
                result = 1.0;
                for(uint32_t i = 2; i <= (uint32_t)value; i++) result *= i;
            }
            break;
        case 'A': result = fabs(value); break;
        case 'Q': result = value * value * value; break;
        case 'R': result = cbrt(value); break;
        default: return;
    }

    if(!calculator_error && !isfinite(result)) calculator_error = true;
    if(!calculator_error) {
        format_number(result, entry, sizeof(entry));
        waiting_for_operand = false;
        just_evaluated = false;
    }
    update_display();
}

static void toggle_scientific_mode(void)
{
    scientific_mode = !scientific_mode;
    second_mode = false;
    lv_label_set_text(mode_label, scientific_mode ? "BASIC" : "SCI");
    lv_label_set_text(second_label, "2nd");
    lv_obj_set_style_bg_color(second_button, lv_color_hex(0x242426), LV_PART_MAIN);
    lv_obj_set_pos(basic_key_panel, scientific_mode ? 228 : 0, 0);
    lv_obj_set_pos(display_panel, scientific_mode ? 204 : 0, 0);
    if(scientific_mode) {
        lv_obj_clear_flag(second_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(angle_button, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(second_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(angle_button, LV_OBJ_FLAG_HIDDEN);
    }
    for(uint32_t i = 0; i < scientific_button_count; i++) {
        if(scientific_mode) lv_obj_clear_flag(scientific_buttons[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(scientific_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    for(uint32_t i = 0; i < 6; i++) {
        static const char * first_labels[] = {"sin", "cos", "tan", "sinh", "cosh", "tanh"};
        lv_label_set_text(scientific_trig_labels[i], second_mode ?
                          (i < 3 ? (i == 0 ? "asin" : i == 1 ? "acos" : "atan") :
                                   (i == 3 ? "asinh" : i == 4 ? "acosh" : "atanh")) :
                          first_labels[i]);
    }
}

static void toggle_second_mode(void)
{
    second_mode = !second_mode;
    lv_obj_set_style_bg_color(second_button,
                              second_mode ? lv_color_hex(0xff9f0a) : lv_color_hex(0x242426),
                              LV_PART_MAIN);
    static const char * first_labels[] = {"sin", "cos", "tan", "sinh", "cosh", "tanh"};
    static const char * second_labels[] = {"asin", "acos", "atan", "asinh", "acosh", "atanh"};
    for(uint32_t i = 0; i < 6; i++)
        lv_label_set_text(scientific_trig_labels[i], second_mode ? second_labels[i] : first_labels[i]);
}

static void toggle_angle_mode(void)
{
    radians_mode = !radians_mode;
    lv_label_set_text(angle_label, radians_mode ? "RAD" : "DEG");
}

static void input_equals(void)
{
    if(calculator_error) return;
    if(pending_operator && !waiting_for_operand) {
        double right = strtod(entry, NULL);
        double result;
        if(!calculate(accumulator, pending_operator, right, &result)) {
            calculator_error = true;
            pending_operator = 0;
            update_display();
            return;
        }
        last_left_operand = accumulator;
        last_operator = pending_operator;
        last_operand = right;
        has_last_operation = true;
        pending_operator = 0;
        accumulator = result;
        format_number(result, entry, sizeof(entry));
        waiting_for_operand = true;
        just_evaluated = true;
        update_display();
    }
    else if(just_evaluated && has_last_operation) {
        double result;
        last_left_operand = strtod(entry, NULL);
        if(!calculate(last_left_operand, last_operator, last_operand, &result)) {
            calculator_error = true;
            update_display();
            return;
        }
        accumulator = result;
        format_number(result, entry, sizeof(entry));
        update_display();
    }
}

static void calculator_key_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const calculator_key_t * key = lv_event_get_user_data(event);
    if(key == NULL) return;

    if(key->key >= '0' && key->key <= '9') input_digit(key->key);
    else switch(key->key) {
        case '.': input_decimal(); break;
        case 'C':
            if(lv_label_get_text(clear_label)[0] == 'A') reset_calculator();
            else clear_entry();
            break;
        case 'g': input_sign(); break;
        case '%': input_percent(); break;
        case '+': case '-': case '*': case '/': case '^': input_operator(key->key); break;
        case '=': input_equals(); break;
        case 'M': toggle_scientific_mode(); break;
        case 'X': toggle_second_mode(); break;
        case 'D': toggle_angle_mode(); break;
        case 'p': input_constant(CALC_PI); break;
        case 'm': input_constant(2.71828182845904523536); break;
        case 's': case 'c': case 't': case 'u': case 'v': case 'w': case 'n': case 'l':
        case 'r': case 'q': case 'i': case 'e': case 'E': case '!':
        case 'A': case 'Q': case 'R':
            input_unary(key->key);
            break;
    }
}

static lv_obj_t * create_calculator_key(lv_obj_t * parent, const char * text,
                                        char key, lv_coord_t x, lv_coord_t y,
                                        lv_coord_t width, lv_coord_t height,
                                        lv_color_t background, lv_color_t foreground,
                                        uint8_t font_size)
{
    if(key_data_count >= sizeof(key_data) / sizeof(key_data[0])) return NULL;
    calculator_key_t * data = &key_data[key_data_count++];
    data->key = key;

    lv_obj_t * button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, height == CALC_KEY_SIZE ? LV_RADIUS_CIRCLE : 12,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, background, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

    lv_obj_t * label = lv_label_create(button);
    lv_label_set_text(label, text);
    const lv_font_t * font = font_size == 1 ? &lv_font_montserrat_14 :
                             font_size == 2 ? &lv_font_montserrat_20 : calculator_key_font;
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, foreground, LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, calculator_key_event_cb, LV_EVENT_CLICKED, data);

    if(key == 'C') clear_label = label;
    if(key == 'M') mode_label = label;
    if(key == 'D') angle_label = label;
    if(key == '/' || key == '*' || key == '-' || key == '+' || key == '^') {
        uint32_t index = key == '/' ? 0 : key == '*' ? 1 : key == '-' ? 2 : key == '+' ? 3 : 4;
        operator_buttons[index] = button;
        operator_labels[index] = label;
    }
    return button;
}

static void create_scientific_key(const char * text, char key, uint8_t col,
                                  uint8_t row, uint8_t trig_index)
{
    lv_color_t background = lv_color_hex(0x505050);
    lv_obj_t * button = create_calculator_key(
        calculator_root, text, key,
        SCI_KEY_X + (lv_coord_t)col * SCI_KEY_STEP_X,
        SCI_KEY_Y + (lv_coord_t)row * SCI_KEY_STEP_Y,
        SCI_KEY_WIDTH, SCI_KEY_HEIGHT, background, lv_color_white(), 1);
    if(button == NULL || scientific_button_count >=
       sizeof(scientific_buttons) / sizeof(scientific_buttons[0])) return;
    scientific_buttons[scientific_button_count++] = button;
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    if(trig_index < 6) scientific_trig_labels[trig_index] = lv_obj_get_child(button, 0);
}


lv_obj_t * ios_calculator_create(lv_obj_t * parent, const lv_font_t * key_font)
{
    calculator_key_font = key_font;
    calculator_display_font = (lv_font_t *)hachimi_ui_font(42);
    if(calculator_display_font == NULL) calculator_display_font = &lv_font_montserrat_20;
    key_data_count = 0;
    scientific_button_count = 0;
    scientific_mode = false;
    second_mode = false;
    radians_mode = false;
    memset(operator_buttons, 0, sizeof(operator_buttons));
    memset(operator_labels, 0, sizeof(operator_labels));
    memset(scientific_buttons, 0, sizeof(scientific_buttons));
    memset(scientific_trig_labels, 0, sizeof(scientific_trig_labels));

    calculator_root = lv_obj_create(parent);
    lv_obj_remove_style_all(calculator_root);
    lv_obj_set_pos(calculator_root, 0, 0);
    lv_obj_set_size(calculator_root, 800, 480);
    lv_obj_set_style_bg_color(calculator_root, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(calculator_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(calculator_root, LV_OBJ_FLAG_SCROLLABLE);

    display_panel = lv_obj_create(calculator_root);
    lv_obj_remove_style_all(display_panel);
    lv_obj_set_size(display_panel, 800, 480);
    lv_obj_clear_flag(display_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    expression_label = lv_label_create(display_panel);
    lv_obj_set_pos(expression_label, 240, 59);
    lv_obj_set_size(expression_label, 320, 18);
    lv_label_set_long_mode(expression_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(expression_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(expression_label, lv_color_hex(0x8e8e93), LV_PART_MAIN);
    lv_obj_set_style_text_align(expression_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_clear_flag(expression_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    display_label = lv_label_create(display_panel);
    lv_obj_set_pos(display_label, 220, 78);
    lv_obj_set_size(display_label, 340, 48);
    lv_label_set_long_mode(display_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(display_label, calculator_display_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(display_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(display_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_clear_flag(display_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    basic_key_panel = lv_obj_create(calculator_root);
    lv_obj_remove_style_all(basic_key_panel);
    lv_obj_set_size(basic_key_panel, 800, 480);
    lv_obj_clear_flag(basic_key_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    static const char * top_text[] = {"AC", "+/−", "%", "÷"};
    static const char top_keys[] = {'C', 'g', '%', '/'};
    static const char * row_text[4][4] = {
        {"7", "8", "9", "×"},
        {"4", "5", "6", "−"},
        {"1", "2", "3", "+"},
        {"0", ".", "="}
    };
    static const char row_keys[4][4] = {
        {'7', '8', '9', '*'},
        {'4', '5', '6', '-'},
        {'1', '2', '3', '+'},
        {'0', '.', '=', 0}
    };
    lv_color_t control = lv_color_hex(0xa5a5a5);
    lv_color_t dark = lv_color_hex(0x333333);
    lv_color_t orange = lv_color_hex(0xff9f0a);
    lv_coord_t y = CALC_KEY_Y;

    for(uint32_t col = 0; col < 4; col++) {
        lv_color_t bg = col == 3 ? orange : control;
        lv_color_t fg = col == 3 ? lv_color_white() : lv_color_black();
        create_calculator_key(basic_key_panel, top_text[col], top_keys[col],
                              CALC_KEY_X + (lv_coord_t)col * CALC_KEY_STEP_X, y,
                              CALC_KEY_SIZE, CALC_KEY_SIZE, bg, fg, 0);
    }
    for(uint32_t row = 0; row < 3; row++) {
        y += CALC_KEY_STEP_Y;
        for(uint32_t col = 0; col < 4; col++) {
            bool op = col == 3;
            create_calculator_key(basic_key_panel, row_text[row][col], row_keys[row][col],
                                  CALC_KEY_X + (lv_coord_t)col * CALC_KEY_STEP_X, y,
                                  CALC_KEY_SIZE, CALC_KEY_SIZE, op ? orange : dark,
                                  lv_color_white(), 0);
        }
    }

    y += CALC_KEY_STEP_Y;
    create_calculator_key(basic_key_panel, "0", '0', CALC_KEY_X, y,
                          CALC_KEY_SIZE * 2 + 12, CALC_KEY_SIZE, dark, lv_color_white(), 0);
    create_calculator_key(basic_key_panel, row_text[3][1], row_keys[3][1],
                          CALC_KEY_X + 2 * CALC_KEY_STEP_X, y,
                          CALC_KEY_SIZE, CALC_KEY_SIZE, dark, lv_color_white(), 0);
    create_calculator_key(basic_key_panel, row_text[3][2], row_keys[3][2],
                          CALC_KEY_X + 3 * CALC_KEY_STEP_X, y,
                          CALC_KEY_SIZE, CALC_KEY_SIZE, orange, lv_color_white(), 0);

    create_calculator_key(calculator_root, "SCI", 'M', 38, 78, 66, 32,
                          lv_color_hex(0x242426), lv_color_hex(0xd7d7dc), 1);
    second_button = create_calculator_key(calculator_root, "2nd", 'X', 110, 78, 54, 32,
                                           lv_color_hex(0x242426), lv_color_hex(0xd7d7dc), 1);
    second_label = lv_obj_get_child(second_button, 0);
    angle_button = create_calculator_key(calculator_root, "DEG", 'D', 170, 78, 54, 32,
                                          lv_color_hex(0x242426), lv_color_hex(0xd7d7dc), 1);
    lv_obj_add_flag(second_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(angle_button, LV_OBJ_FLAG_HIDDEN);

    create_scientific_key("sin", 's', 0, 0, 0);
    create_scientific_key("cos", 'c', 1, 0, 1);
    create_scientific_key("tan", 't', 2, 0, 2);
    create_scientific_key("sinh", 'u', 0, 1, 3);
    create_scientific_key("cosh", 'v', 1, 1, 4);
    create_scientific_key("tanh", 'w', 2, 1, 5);
    create_scientific_key("ln", 'n', 0, 2, 255);
    create_scientific_key("log", 'l', 1, 2, 255);
    create_scientific_key("sqrt", 'r', 2, 2, 255);
    create_scientific_key("x^2", 'q', 0, 3, 255);
    create_scientific_key("x^y", '^', 1, 3, 255);
    create_scientific_key("1/x", 'i', 2, 3, 255);
    create_scientific_key("e^x", 'e', 0, 4, 255);
    create_scientific_key("10^x", 'E', 1, 4, 255);
    create_scientific_key("x!", '!', 2, 4, 255);
    create_scientific_key("pi", 'p', 0, 5, 255);
    create_scientific_key("e", 'm', 1, 5, 255);
    create_scientific_key("|x|", 'A', 2, 5, 255);
    create_scientific_key("x^3", 'Q', 0, 6, 255);
    create_scientific_key("cbrt", 'R', 1, 6, 255);

    reset_calculator();
    ios_status_bar_create(calculator_root, lv_color_white());
    lv_obj_add_flag(calculator_root, LV_OBJ_FLAG_HIDDEN);
    return calculator_root;
}

static lv_obj_t * calculator_app_create(lv_obj_t * parent)
{
    return ios_calculator_create(parent, hachimi_ui_font(22));
}

const hachimi_app_module_t hachimi_calculator_app = {
    "calculator", calculator_app_create, NULL, NULL, NULL, NULL
};
