#ifndef MENU_H
#define MENU_H

/*menu states*/
enum MENU
{
    MENU_NONE1,
    MENU_NONE2,
    MENU_NG,
    MENU_NG1,
    MENU_NG2,
    MENU_FILE_SELECT,
    MENU_FILE_SELECT1,
    MENU_FILE_SELECT2,
    MENU_FILE_SELECT_EXIT,
    MENU_DIALOG1,
    MENU_DIALOG2,

    // 8bit menu entries
    MENU_8BIT_ABOUT1,
    MENU_8BIT_ABOUT2,
    MENU_8BIT_CHRTEST1,
    MENU_8BIT_CHRTEST2
};

typedef struct {
    char *title;
    uint8_t flags;
    uint32_t timer;
    uint8_t stdexit;
} menu_page_t;

typedef struct {
    char *item;
    char stipple;
    char active;
    char newpage;
    uint8_t newsub;
    char page;
} menu_item_t;

#define MENU_ACT_NONE -1
#define MENU_ACT_GET   0
#define MENU_ACT_SEL   1
#define MENU_ACT_BKSP  2
#define MENU_ACT_LEFT  3
#define MENU_ACT_RIGHT 4
#define MENU_ACT_PLUS  5
#define MENU_ACT_MINUS 6

#define MENU_STD_NONE_EXIT  0
#define MENU_STD_EXIT       1
#define MENU_STD_SPACE_EXIT 2
#define MENU_STD_COMBO_EXIT 3

#define MENU_PAGE_ENTER 0
#define MENU_PAGE_EXIT 1

#define MENU_DIALOG_OK 1
#define MENU_DIALOG_YESNO 2
#define MENU_DIALOG_TIMER 4


typedef char (*menu_get_items_t)(uint8_t, char, menu_item_t*);
typedef char (*menu_get_page_t)(uint8_t, char, menu_page_t*);
typedef char (*menu_key_event_t)(uint8_t);
typedef char (*menu_select_file_t)(uint8_t, const char*);
typedef char (*menu_dialog_t)(uint8_t);

void DialogBox(const char *message, char options, menu_dialog_t);
void SelectFile(char* pFileExt, unsigned char Options, unsigned char MenuSelect, char chdir);
void SelectFileNG(char *pFileExt, unsigned char Options, menu_select_file_t callback, char chdir);
void SetupSystemMenu();
void SetupMenu(menu_get_page_t, menu_get_items_t, menu_key_event_t);
void CloseMenu();
void ResetMenu();
void ClosePage();
void ChangePage(char);

void HandleUI(void);
void ErrorMessage(const char *message, unsigned char code);
void InfoMessage(const char *message);

extern const char *config_cpu_msg[];
extern const char *config_autofire_msg[];

enum HelpText_Message {HELPTEXT_NONE,HELPTEXT_MAIN,HELPTEXT_HARDFILE,HELPTEXT_CHIPSET,HELPTEXT_MEMORY,HELPTEXT_VIDEO,HELPTEXT_FEATURES,HELPTEXT_INPUT};
extern const char *helptexts[];
extern const char* HELPTEXT_SPACER;
extern char helptext_custom[450];
extern const char *helptext;

#endif
