#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <testkit.h>
#include "labyrinth.h"

//git stash
//git pull origin M2 --no-rebase
//ctrl+x保存退出
//git stash pop
int main(int argc, char *argv[]) {
    // TODO: Implement this function
    Labyrinth labyrinth={0};
    char Id = '0';
    for(int i = 1; i < argc; i++){
        if(strcmp(argv[i],"--map")==0 || strcmp(argv[i],"-m")==0){//处理命令行参数
            if(i + 1 >= argc || !loadMap(&labyrinth,argv[i+1])){
                return 1;
            }
            i++;
        }
        else if(strcmp(argv[i],"--player")==0 || strcmp(argv[i],"-p")==0){
            if(i + 1 >= argc || strlen(argv[i+1]) != 1  || isValidPlayer(argv[i+1][0])){//注意，这是个字符串，不是字符，需要取第一个字符
                return 1;
            }
            Id = argv[i+1][0];
            i++;
        }
        else if(strcmp(argv[i],"--move")==0){
            if(i + 1 >= argc || !movePlayer(&labyrinth,Id,argv[i+1])){
                return 1;
            }
            i++;
        }
        else if(strcmp(argv[i],"--version")==0){
            if(argc != 2){//不止一个参数
                return 1;
            }
            printf("Labyrinth Game\n");
            return 0;
        }
    }
    return 0;
}

void printUsage() {
    printf("Usage:\n");
    printf("  labyrinth --map map.txt --player id\n");
    printf("  labyrinth -m map.txt -p id\n");
    printf("  labyrinth --map map.txt --player id --move direction\n");
    printf("  labyrinth --version\n");
}

bool isValidPlayer(char playerId) {//判断非法ID
    if(playerId < '0' || playerId > '9'){
        return true;
    }
    return false;
}

bool loadMap(Labyrinth *labyrinth, const char *filename) {
    const char *dot = strrchr(filename, '.');   
    if (dot == NULL || strcmp(dot, ".txt") != 0) {
        return false;//文件格式错误
    }
    FILE *fp = fopen(filename,"r");
    if(fp == NULL){
        return false;//打开文件失败
    }
    
    char line[MAX_COLS + 2];
    int expected_cols = -1;
    int row = 0;

    while (fgets(line, sizeof(line), fp) && row < MAX_ROWS) {//每行相同字数
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0'; 
        }
        if(len > MAX_COLS || len == 0){
            return false;
        }
        if (expected_cols == -1) {
            expected_cols = len;
            labyrinth->cols = expected_cols;//列数
        } else {
            if (len != expected_cols) { //检查是否与第一行列数一致
                return false;
            }
        }
        strncpy(labyrinth->map[row], line, expected_cols);
        labyrinth->map[row][expected_cols] = '\0'; 
        row++;
    }

    labyrinth->rows = row; //行数
    fclose(fp);

    if (labyrinth->rows == 0 || labyrinth->rows > MAX_ROWS) {//地图过大
        return false;
    }

    if (!isConnected(labyrinth)) {//不连通
        return false;
    }

    for(int i = 0; i < labyrinth->rows; i++){
        for(int j = 0; j < labyrinth->cols; j++){//有其他非法内容
            if(labyrinth->map[i][j] != '#' && labyrinth->map[i][j] != '.' &&  isValidPlayer(labyrinth->map[i][j])){
                return false;
            }  
        }
    }

    for(int i = 0; i < labyrinth->rows; i++){
        for(int j = 0; j < labyrinth->cols; j++){
            printf("%c",labyrinth->map[i][j]);
        }
        printf("\n");
    }
    return true;
}

Position findPlayer(Labyrinth *labyrinth, char playerId) {
    Position pos = {-1, -1};
    for(int i = 0; i < labyrinth->rows; i++){
        for(int j = 0; j < labyrinth->cols; j++){
            if(labyrinth->map[i][j] == playerId){
                pos.row = i;
                pos.col = j;
                return pos;
            }
        }
    }
    return pos;
}

Position findFirstEmptySpace(Labyrinth *labyrinth) {
    Position pos = {-1, -1};
    for(int i = 0; i < labyrinth->rows; i++){
        for(int j = 0; j < labyrinth->cols; j++){
            if(labyrinth->map[i][j] == '.'){//找到第一个空地，用于放置玩家
                pos.row = i;
                pos.col = j;
                return pos;
            }
        }
    }
    return pos;
}

bool isEmptySpace(Labyrinth *labyrinth, int row, int col) {
    return labyrinth->map[row][col] == '#' ? false : true;//玩家视为空地，用于地图连通性判断
}

bool movePlayer(Labyrinth *labyrinth, char playerId, const char *direction) {
    Position pos = findPlayer(labyrinth,playerId);
    if(pos.row == -1 && pos.col == -1){
        Position p = findFirstEmptySpace(labyrinth);
        if(p.row == -1 && p.col == -1){//找不到空地
            return false;
        }
        labyrinth->map[p.row][p.col] = playerId;//放置不在图上的玩家
        pos.row = p.row;
        pos.col = p.col;
    }

    if(strcmp(direction,"up") == 0){
        if(pos.row == 0 || labyrinth->map[pos.row-1][pos.col] == '#' || (labyrinth->map[pos.row-1][pos.col] >= '0' && labyrinth->map[pos.row-1][pos.col] <= '9')){
            return false;
        }//判断是否越界或者碰到墙或者碰到其他玩家
        labyrinth->map[pos.row][pos.col] = '.';
        labyrinth->map[pos.row-1][pos.col] = playerId;
    }
    else if(strcmp(direction,"down") == 0){
        if(pos.row == labyrinth->rows-1 || labyrinth->map[pos.row+1][pos.col] == '#' || (labyrinth->map[pos.row+1][pos.col] >= '0' && labyrinth->map[pos.row+1][pos.col] <= '9')){
            return false;
        }
        labyrinth->map[pos.row][pos.col] = '.';
        labyrinth->map[pos.row+1][pos.col] = playerId;
    }
    else if(strcmp(direction,"left") == 0){
        if(pos.col == 0 || labyrinth->map[pos.row][pos.col-1] == '#' || (labyrinth->map[pos.row][pos.col-1] >= '0' && labyrinth->map[pos.row][pos.col-1] <= '9')){
            return false;
        }
        labyrinth->map[pos.row][pos.col] = '.';
        labyrinth->map[pos.row][pos.col-1] = playerId;
    }
    else if(strcmp(direction,"right") == 0){
        if(pos.col == labyrinth->cols-1 || labyrinth->map[pos.row][pos.col+1] == '#' || (labyrinth->map[pos.row][pos.col+1] >= '0' && labyrinth->map[pos.row][pos.col+1] <= '9')){
            return false;
        }
        labyrinth->map[pos.row][pos.col] = '.';
        labyrinth->map[pos.row][pos.col+1] = playerId;
    }
    else{
        return false;
    }
    return true;
}

bool saveMap(Labyrinth *labyrinth, const char *filename) {
    // TODO: Implement this function
    FILE *fp = fopen(filename,"w");
    if(fp == NULL){
        return false;
    }
    for(int i = 0; i < labyrinth->rows; i++){
        for(int j = 0; j < labyrinth->cols; j++){
            fprintf(fp,"%c",labyrinth->map[i][j]);
        }
        fprintf(fp,"\n");
    }
    fclose(fp);
    return true;
}

// Check if all empty spaces are connected using DFS
void dfs(Labyrinth *labyrinth, int row, int col, bool visited[MAX_ROWS][MAX_COLS]) {
    if (row < 0 || row >= labyrinth->rows || col < 0 || col >= labyrinth->cols) return;
    if (visited[row][col] || !isEmptySpace(labyrinth, row, col)) return;//访问过或者不是空地，返回
    visited[row][col] = true;
    dfs(labyrinth, row-1, col, visited); 
    dfs(labyrinth, row+1, col, visited); 
    dfs(labyrinth, row, col-1, visited); 
    dfs(labyrinth, row, col+1, visited); 
}

bool isConnected(Labyrinth *labyrinth) {
    if (labyrinth->rows == 0 || labyrinth->cols == 0) return false;

    bool visited[MAX_ROWS][MAX_COLS] = {false};
    bool start = false;

    for (int i = 0; i < labyrinth->rows && !start; ++i) {
        for (int j = 0; j < labyrinth->cols && !start; ++j) {
            if (isEmptySpace(labyrinth, i, j)) {
                dfs(labyrinth, i, j, visited);//从start开始遍历，把相连空地标记出来
                start = true;
            }
        }
    }
    for (int i = 0; i < labyrinth->rows; ++i) {//验证连通
        for (int j = 0; j < labyrinth->cols; ++j) {
            if (isEmptySpace(labyrinth, i, j) && !visited[i][j]) {
                return false;
            }
        }
    }
    return true;
}
