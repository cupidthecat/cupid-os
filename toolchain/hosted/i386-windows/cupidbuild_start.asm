[BITS 32]

extern __imp_CreateDirectoryA
extern __imp_CreateProcessA
extern __imp_FindClose
extern __imp_FindFirstFileA
extern __imp_FindNextFileA
extern __imp_GetCurrentProcessId
extern __imp_GetExitCodeProcess
extern __imp_GetFileAttributesA
extern __imp_GetFileInformationByHandle
extern __imp_OpenProcess
extern __imp_RemoveDirectoryA
extern __imp_SetHandleInformation
extern __imp_TerminateProcess
extern __imp_WaitForSingleObject
extern __imp_NtCreateFile
extern __imp_NtQueryDirectoryFile
extern __imp_NtSetInformationFile

global cupid_windows_create_directory:function
global cupid_windows_create_process:function
global cupid_windows_find_close:function
global cupid_windows_find_first_file:function
global cupid_windows_find_next_file:function
global cupid_windows_get_current_process_id:function
global cupid_windows_get_exit_code_process:function
global cupid_windows_get_file_attributes:function
global cupid_windows_get_file_information:function
global cupid_windows_open_process:function
global cupid_windows_remove_directory:function
global cupid_windows_set_handle_information:function
global cupid_windows_terminate_process:function
global cupid_windows_wait_for_single_object:function
global cupid_windows_nt_create_file:function
global cupid_windows_nt_query_directory_file:function
global cupid_windows_nt_set_information_file:function

section .text

cupid_windows_create_directory:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_CreateDirectoryA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_create_process:
 push ebp
 mov ebp, esp
 push dword [ebp + 44]
 push dword [ebp + 40]
 push dword [ebp + 36]
 push dword [ebp + 32]
 push dword [ebp + 28]
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_CreateProcessA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_find_close:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_FindClose]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_find_first_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_FindFirstFileA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_find_next_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_FindNextFileA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_get_current_process_id:
 call dword [__imp_GetCurrentProcessId]
 ret

cupid_windows_get_exit_code_process:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetExitCodeProcess]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_get_file_attributes:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_GetFileAttributesA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_get_file_information:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_GetFileInformationByHandle]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_open_process:
 push ebp
 mov ebp, esp
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_OpenProcess]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_remove_directory:
 push ebp
 mov ebp, esp
 push dword [ebp + 8]
 call dword [__imp_RemoveDirectoryA]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_set_handle_information:
 push ebp
 mov ebp, esp
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_SetHandleInformation]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_terminate_process:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_TerminateProcess]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_wait_for_single_object:
 push ebp
 mov ebp, esp
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_WaitForSingleObject]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_nt_create_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 48]
 push dword [ebp + 44]
 push dword [ebp + 40]
 push dword [ebp + 36]
 push dword [ebp + 32]
 push dword [ebp + 28]
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_NtCreateFile]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_nt_query_directory_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 48]
 push dword [ebp + 44]
 push dword [ebp + 40]
 push dword [ebp + 36]
 push dword [ebp + 32]
 push dword [ebp + 28]
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_NtQueryDirectoryFile]
 mov esp, ebp
 pop ebp
 ret

cupid_windows_nt_set_information_file:
 push ebp
 mov ebp, esp
 push dword [ebp + 24]
 push dword [ebp + 20]
 push dword [ebp + 16]
 push dword [ebp + 12]
 push dword [ebp + 8]
 call dword [__imp_NtSetInformationFile]
 mov esp, ebp
 pop ebp
 ret
