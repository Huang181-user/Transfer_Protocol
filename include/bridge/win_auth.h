#ifndef WIN_AUTH_H
#define WIN_AUTH_H
#ifdef __cplusplus
extern "C" {
#endif
    int win_load_cred(char* user, char* pass);
    int win_prompt_cred(char* user, char* pass, int* save);
    void win_save_cred(const char* user, const char* pass);
    void win_delete_cred();
#ifdef __cplusplus
}
#endif
#endif
