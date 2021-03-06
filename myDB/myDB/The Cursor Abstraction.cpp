//
//  The Cursor Abstraction.cpp
//  myDB
//
//  Created by xiaoyuyu on 2022/2/27.
//  Copyright © 2022 xiaoyuyu. All rights reserved.
//

// 本版本只支持三个语句:  1)insert id username email 2)select 3).exit

// 尝试将数据库的数据存放到本地文件中，来实现持久化
// 关于持久化：1)启动时，从本地文件加载数据 2)结束时，更新本地文件
// 实现1)数据的加载是很容易的，直接导入数据即可
// 实现2)，由于目前还不支持delete，是一个append only的数据库，更新文件，总体来说也不难

// 增加了一个游标cusor结构，为了后续实现b树做铺垫

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cerrno>
#include <stdlib.h>

using namespace std;

const int COLUMN_USERNAME_SIZE = 32;
const int COLUMN_EMAIL_SIZE = 255;

typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
} StatementType;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;

const int MAX_PAGES = 100;
const int PAGE_SIZE = 4096;

// struct of table in memory
class Table {
public:
    fstream file_descriptor;    // 存储文件的标识符
    size_t file_length;    // 存储文件的字节数
    size_t num_rows;    // the number of total rows in the table
    void* page[MAX_PAGES];  // an array of pages, each pointer reflects a page
    
    Table() : file_length(0),num_rows(0),page{nullptr} {}
    ~Table() {
        if (file_descriptor)
            file_descriptor.close();
        for (int i = 0; i < MAX_PAGES; i++)
            if (page[i] != nullptr) {
                operator delete(page[i]);
                page[i] = nullptr;
            }
    }
};

// struct of a row
// reflect data of statement, etc.id , username, email
class Row {
public:
    int id;
    char username[COLUMN_USERNAME_SIZE];
    char email[COLUMN_EMAIL_SIZE];
};

ostream& operator<<(ostream& os, Row row) {
    os << "id:" << row.id << "\t" << "username:" << row.username << "\t" << "email:" << row.email;
    return os;
}

class Cursor{
public:
    Table* table;
    size_t row_num; // 指向的行号
    bool end_of_table;  // 判断当前游标是否指向最后一行
    Cursor() : table(nullptr),row_num(0),end_of_table(false) {}
    ~Cursor() {
        delete table;
        table = nullptr;
    }
    Cursor(const Cursor& p) {
        this->row_num = p.row_num;
        this->end_of_table = p.end_of_table;
        this->table = p.table;  // 有个fstream，暂时浅拷贝
    }
    
    Cursor& operator=(const Cursor& p) {
        if (this->table != p.table) {
            delete this->table;
            this->table = nullptr;
        }
        this->row_num = p.row_num;
        this->end_of_table = p.end_of_table;
        this->table = p.table;  // 有个fstream，暂时浅拷贝
        return *this;
    }
    
    // ++Cusor
    Cursor& operator++() {
        row_num++;
        end_of_table = row_num == table->num_rows;
        return *this;
    }
    
    // Cusor++
    Cursor operator++(int n) {
        Cursor temp = *this;
        operator++();
        return temp;
    }
};

// 返回一个指向table第一行的游标
Cursor* table_start(Table& table) {
    Cursor* cusor = new Cursor;
    cusor->row_num = 0;
    cusor->end_of_table = table.num_rows == 0;
    cusor->table = &table;
    return cusor;
}
// 返回一个指向table最后一行的游标
Cursor* table_end(Table& table) {
    Cursor* cusor = new Cursor;
    cusor->row_num = table.num_rows;
    cusor->end_of_table = true;
    cusor->table = &table;
    return cusor;
}

class Statement {
public:
    Row* row;   // a pointer to a row
    StatementType type; //type of a statement, etc.insert, select
    Statement() : type(STATEMENT_SELECT) {
        row = new Row;
    }
    Statement(const Statement& p) {
        this->type = p.type;
        this->row = new Row(*(p.row));
    }
    Statement& operator=(const Statement& p) {
        if (this == &p)
            return *this;
        delete this->row;
        this->row = nullptr;
        this->type = p.type;
        this->row = new Row(*(p.row));
        return *this;
    }
    ~Statement() {
        delete row;
        row = nullptr;
    }
};

#define size_of_attribute(Struct,Attribute) sizeof(((Struct*)0)->Attribute)
const int ID_SIZE = size_of_attribute(Row, id);
const int USERNAME_SIZE = size_of_attribute(Row, username);
const int EMAIL_SIZE = size_of_attribute(Row, email);
const int ID_OFFSET = 0;
const int USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const int EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const int ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const int ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const int TABLE_MAX_ROWS = ROWS_PER_PAGE * MAX_PAGES;

class myBuffer {
public:
    string buff;
    size_t bufferSize;
    ssize_t inputSize;
    
    myBuffer() : bufferSize(0),inputSize(0) {
        buff = "";
    }
    
    ~myBuffer() {}
};

myBuffer* newInputBuffer() {
    myBuffer * buffer = new myBuffer;
    return buffer;
}

void closeBuffer(myBuffer * buffer) {
    delete buffer;
    buffer = nullptr;
}

void printPrompt() {
    cout << "DB > ";
}

void readInput(myBuffer * buff) {
    getline(cin,buff->buff);
    int len = (int)buff->buff.size();
    buff->bufferSize = buff->inputSize = len;
}

MetaCommandResult do_meta_command(string command) {
    return META_COMMAND_UNRECOGNIZED_COMMAND;   // 目前除了.exit，识别不了任何指令
}

PrepareResult prepare_statement(string command, Statement& statement) {
    // 将对应的数据插入到Statement的结构中
    if (strncmp(command.c_str(), "select", 6) == 0) { // 目前只支持全部select
        statement.type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    } else if (strncmp(command.c_str(), "insert", 6) == 0) {
        statement.type = STATEMENT_INSERT;
        int args_assigned = sscanf(&command[0], "insert %d %s %s",
                                   &(statement.row->id),
                                   statement.row->username,
                                   statement.row->email
        );
        if (args_assigned < 3) {
        return PREPARE_SYNTAX_ERROR;
        }
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

// 获得对应页的地址
void* get_page(Table& table, size_t page_num) {
    // 判断读取的页数是否超出了table能存储的最大页数
    if (page_num > MAX_PAGES) {
        cout << "Tried to fetch page number out of bounds. " << page_num << " > " << MAX_PAGES << endl;
        exit(EXIT_FAILURE);
    }
    // 如果想获取的页数为空，需要新开辟空间
    // 相当于做了个页缓存，没有直接将文件中的数据全部读入内存中
    if (table.page[page_num] == nullptr) {
        // Cache miss. Allocate memory and load from file.
        table.page[page_num] = operator new(PAGE_SIZE);
        //  当前文件最多有多少页
        size_t num_pages = table.file_length / PAGE_SIZE;
        if (table.file_length % PAGE_SIZE)
            num_pages++;    // 当前文件的page数
        // 防止飞页
        if (page_num <= num_pages) {
            table.file_descriptor.seekp(page_num * PAGE_SIZE, ios::beg);
            if (!table.file_descriptor) {
                cout << "Error seeking file" << endl;
                exit(EXIT_FAILURE);
            }
            // 将大小为一页的数据完整的存入内存中的对应的"页内"
            // cout << table.page[page_num] << endl;
            table.file_descriptor.read((char*)table.page[page_num], PAGE_SIZE);
            if (table.file_descriptor.eof()) {
                table.file_descriptor.clear();
                return table.page[page_num];
            }
            if (!table.file_descriptor) {
                cout << "Error reading file: " << strerror(errno) << endl;
                exit(EXIT_FAILURE);
            }
        }
    }
    return table.page[page_num];
}

// 求行所在地址
void* cursor_value(Cursor* cursor) {
    // 确定应该插入或读取的位置在第几页
    size_t num_rows = cursor->row_num;
    size_t page_num = num_rows / ROWS_PER_PAGE;
    void* page = get_page(*(cursor->table), page_num);
    // 判断应该插入的页内偏移量
    int row_offset = num_rows % ROWS_PER_PAGE;
    int byte_offset = row_offset * ROW_SIZE;
    return (char*)page + byte_offset;
}

// cusor所指向的行号+1，可以直接使用++Cursor/Cursor++
void cursor_advance(Cursor* cursor) {
    cursor->row_num += 1;
    if (cursor->row_num >= cursor->table->num_rows) {
        cursor->end_of_table = true;
    }
}

// 序列化 insert
void serialize_row(Row* source, void* memory) {
    memcpy((char*)memory + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy((char*)memory + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy((char*)memory + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

// 反序列化 select
void deserialize_row(Row* source, void* memory) {
    memcpy(&(source->id), (char*)memory + ID_OFFSET, ID_SIZE);
    memcpy(&(source->username), (char*)memory + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(source->email), (char*)memory + EMAIL_OFFSET, EMAIL_SIZE);
}

ExecuteResult execute_insert(Statement& statement, Table& table) {
    // 判断是否还有存储的空间
    if (table.num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }
    // 需要获得当前数据应该插入的位置
    Cursor* cursor = table_end(table);
    void* destination = cursor_value(cursor);
    // cout << destination << endl;
    // 进行序列话，也就是插入数据
    serialize_row(statement.row, destination);
    table.num_rows++;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement& statement, Table& table) {
    Row* row = new Row;
    Cursor* cursor = table_start(table);
    while (!cursor->end_of_table) {
        void* memory = cursor_value(cursor);
        // cout << memory << endl;
        deserialize_row(row, memory);
        cout << *row << endl;
        // cursor_advance(cursor);
        ++(*cursor);
    }
    delete row;
    row = nullptr;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement& statement, Table& table) {
    switch (statement.type) {
        case STATEMENT_SELECT:
            return execute_select(statement, table);
            break;
        case STATEMENT_INSERT:
            return execute_insert(statement,table);
            break;
    }
}

// I’m renaming new_table() to db_open() because it now has the effect of opening a connection to the database. By opening a connection,I mean:
// opening the database file
// initializing a pager data structure
// initializing a table data structure
// 打开本地文件，将table绑定到对应的文件上
void db_open(Table& db, string path) {
    db.file_descriptor.open(path, ios::in | ios::out);
    if (db.file_descriptor) {
        db.file_descriptor.seekp(0,ios_base::end);
        db.file_length = db.file_descriptor.tellg();
        db.file_descriptor.seekp(0,ios_base::beg);
        db.num_rows = db.file_length / ROW_SIZE;
    } else {
        cout << "open database error." << endl;
        exit(EXIT_FAILURE);
    }
}

void pager_flush(Table& db, size_t page_num, size_t size) {
    if (db.page[page_num] == NULL) {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }
    
    db.file_descriptor.seekp(page_num * PAGE_SIZE, ios::beg);
    if (!db.file_descriptor) {
        cout << "Error seeking" << endl;
        exit(EXIT_FAILURE);
    }
    
    // You need to save the 'payload' CONTENT of the string, not the string object as such (which typically contains just a length and a pointer to the actual content)
    db.file_descriptor.write((char*)db.page[page_num], size);
    
    if (!db.file_descriptor) {
        cout << "Error writing" << endl;
        exit(EXIT_FAILURE);
    }
}

// For now, we’ll wait to flush the cache to disk until the user closes the connection to the database. When the user exits, we’ll call a new method called db_close(), which
// flushes the page cache to disk
// closes the database file
// frees the memory for the Pager and Table data structures
void db_close(Table& db) {
    size_t num_full_pages = db.num_rows / ROWS_PER_PAGE;
    for (int i = 0; i < num_full_pages; i++) {
        if (db.page[i] == nullptr)
            continue;
        pager_flush(db, i, PAGE_SIZE);
    }
    // There may be a partial page to write to the end of the file
    // This should not be needed after we switch to a B-tree
    size_t num_additional_rows = db.num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0) {
        size_t page_num = num_full_pages;
        if (db.page[page_num] != nullptr) {
            pager_flush(db, page_num, num_additional_rows * ROW_SIZE);
        }
    }
}

int main(int argc, const char * argv[]) {
    // 创建用来存放内存中数据的table
    Table table;
    // 将本地文件中的数据导入table
    db_open(table, "xxxx");
    // 创建缓冲区
    myBuffer * buff = newInputBuffer();
    while (true) {
        printPrompt();
        // 读取信息
        readInput(buff);
        // 处理信息
        string command = buff->buff;
        // Non-SQL statements like .exit are called “meta-commands”. They all start with a dot, so we check for them and handle them in a separate function.
        // 判断指令是否存在，或者说指令是否符合标准
        if (command[0] == '.') {
            if (command == ".exit") {
                // 退出
                closeBuffer(buff);
                db_close(table);
                return 0;;
            }
            switch (do_meta_command(command)) {
                case META_COMMAND_SUCCESS :
                    continue;
                case META_COMMAND_UNRECOGNIZED_COMMAND :
                    cout << "Unrecognized command : " << command << endl;
                    continue;
            }
        }
        // Next, we add a step that converts the line of input into our internal representation of a statement. This is our hacky version of the sqlite front-end.
        // 将指令转换成字节码
        Statement statement;
        switch (prepare_statement(command,statement)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                cout << "Unrecognized keyword at start of " << command << endl;
                continue;
            case PREPARE_SYNTAX_ERROR:
                cout << "Syntax error. Could not parse statement." << endl;
                continue;
        }

        // Lastly, we pass the prepared statement to execute_statement. This function will eventually become our virtual machine.
        // 虚拟机执行指令对应的字节码
        switch(execute_statement(statement,table)) {
            case (EXECUTE_SUCCESS):
                cout << "Executed." << endl;
                break;
            case (EXECUTE_TABLE_FULL):
                cout << "Error: Table full." << endl;
                break;
        }
    }
    return 0;
}


