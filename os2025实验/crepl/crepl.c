#include <stdio.h>
#include <stdbool.h>


#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#define TEMPLATE "/tmp/crepl_XXXXXX"
#define MAX_DEFS 100

typedef struct {
    char* name;
    void* handle;//描述符
    char* so_filename; //so文件名
} FunctionDef;

FunctionDef function_defs[MAX_DEFS];//储存函数
int num_defs = 0;



// 创建临时文件
int create_temp_file(char* template, char** filename) {

    int fd = mkstemp(template);
    if (fd == -1){
        perror("mkstemp");
        return -1;
    }

    *filename = strdup(template);
    return fd;
}


// 修改后的编译函数，支持链接已有库
bool compile_shared_lib(const char* c_filename, const char** link_files, int link_count) {
    char so_filename[64];
    snprintf(so_filename, sizeof(so_filename), "%s.so", c_filename);
    
    pid_t pid = fork();
    if (pid == 0) {
        char** args = malloc((7 + 2 * link_count) * sizeof(char*));
        int i = 0;
        args[i++] = "gcc";
        args[i++] = "-shared";
        args[i++] = "-fPIC";
        args[i++] = "-x";      
        args[i++] = "c";    
        args[i++] = "-o";
        args[i++] = so_filename;
        args[i++] = c_filename;  


        for (int j = 0; j < link_count; j++) {
            args[i++] = link_files[j]; 
        }
        args[i] = NULL;

        execvp("gcc", args);
        exit(EXIT_FAILURE);
    }
    
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}



// Compile a function definition and load it
bool compile_and_load_function(const char* function_def) {
    char temp_path[64];
    strcpy(temp_path, TEMPLATE);

    char* c_filename = NULL;
    int c_fd = create_temp_file(temp_path, &c_filename);
    if (c_fd == -1) return false;

  
    // 写入函数定义到临时文件
    dprintf(c_fd, "#include <stdint.h>\n");
    for (int i = 0; i < num_defs; i++) {
        dprintf(c_fd, "int %s();\n", function_defs[i].name);
    }
    dprintf(c_fd, "%s\n", function_def);
    close(c_fd);


    // 准备链接参数
    const char* link_files[MAX_DEFS];
    for (int i = 0; i < num_defs; i++) {
        link_files[i] = function_defs[i].so_filename;
    }



    // 编译为共享库
    if (!compile_shared_lib(c_filename,link_files, num_defs)) {
        unlink(c_filename);
        free(c_filename);
        return false;
    }


    // 生成.so路径
    char so_filename[64];
    snprintf(so_filename, sizeof(so_filename), "%s.so", c_filename);


    // 加载共享库
    void* handle = dlopen(so_filename, RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        unlink(c_filename);
        unlink(so_filename);
        free(c_filename);
        return false;
    }

    // 解析函数名
    const char* name_start = strstr(function_def, " ") + 1;
    const char* name_end = strstr(name_start, "(");
    size_t name_len = name_end - name_start;
    char* name = strndup(name_start, name_len);

    // 存储函数定义
    if (num_defs < MAX_DEFS) {
        function_defs[num_defs].name = name;
        function_defs[num_defs].handle = handle;
        num_defs++;
    } else {
        free(name);
        dlclose(handle);
        unlink(c_filename);
        unlink(so_filename);
        free(c_filename);
        return false;
    }

    // 清理临时文件
    unlink(c_filename);
    free(c_filename);
    return true;
}

// Evaluate an expression
bool evaluate_expression(const char* expression, int* result) {
    char temp_path[64];
    strcpy(temp_path, TEMPLATE);

    char* c_filename = NULL;
    int c_fd = create_temp_file(temp_path, &c_filename);
    if (c_fd == -1){
        printf("Step0\n");
        return false;
    } 

    char so_filename[64];
    snprintf(so_filename, sizeof(so_filename), "%s.so", c_filename);

    // 写入表达式包装函数到临时文件
    static int expr_counter = 0;
    char wrapper_name[32];
    snprintf(wrapper_name, sizeof(wrapper_name), "__expr_wrapper_%d", expr_counter++);
   
   
    dprintf(c_fd, "#include <stdint.h>\n");
    for (int i = 0; i < num_defs; i++) {
        dprintf(c_fd, "extern int %s();\n", function_defs[i].name);
    }
    dprintf(c_fd, "int %s() { return %s; }\n", wrapper_name, expression);
    close(c_fd);


    const char* link_files[MAX_DEFS];
    for (int i = 0; i < num_defs; i++) {
        link_files[i] = function_defs[i].so_filename;
    }
    
    if (!compile_shared_lib(c_filename, link_files, num_defs)) {

        unlink(c_filename);
        free(c_filename);
        printf("Step1\n");
        return false;
    }

    // 加载共享库
    void* handle = dlopen(so_filename, RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        unlink(c_filename);
        unlink(so_filename);
        free(c_filename);
        printf("Step2\n");
        return false;
    }

    // 获取包装函数地址
    int (*wrapper_func)() = dlsym(handle, wrapper_name);
    if (!wrapper_func) {
        fprintf(stderr, "dlsym: %s\n", dlerror());
        dlclose(handle);
        unlink(c_filename);
        unlink(so_filename);
        free(c_filename);
        printf("Step3\n");
        return false;
    }

    // 调用包装函数
    *result = wrapper_func();

    // 清理临时文件
    dlclose(handle);
    unlink(c_filename);
    unlink(so_filename);
    free(c_filename);
    return true;

}

int main() {
    char input[256];

    while (1) {
        printf("crepl> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';

        if (strncmp(input, "int ", 4) == 0) {
            if (!compile_and_load_function(input)) {
                printf("Compilation error.\n");
            }
        }
        else {
            int result;
            if (evaluate_expression(input, &result)) {
                printf("= %d.\n", result);
            } else {
                printf("Compilation error.\n");
            }
        }
    }

    // 清理资源
    for (int i = 0; i < num_defs; i++) {
        dlclose(function_defs[i].handle);
        unlink(function_defs[i].so_filename);
        free(function_defs[i].name);
        free(function_defs[i].so_filename);
    }
    return 0;
}



