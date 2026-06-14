use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::Write;
use std::path::Path;

// 
//  AOF 日志命令定义
// 

#[derive(Serialize, Deserialize, Debug)]
pub enum Command {
    AddStudent(Student),
    UpdateStudent(usize, Student),
    DeleteStudent(usize),
    AddTeacher(Teacher),
    UpdateTeacher(usize, Teacher),
    DeleteTeacher(usize),
    AddBook(Book),
    UpdateBook(usize, Book),
    DeleteBook(usize),
}

// 
//  数据模型
// 

#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct Grade {
    pub course: String,
    pub score: f64,
}

#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct Student {
    pub id: String,
    pub name: String,
    pub gender: String,
    pub age: String,
    pub department: String,
    pub major: String,
    pub class_name: String,
    pub grades: Vec<Grade>,
}

#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct Teacher {
    pub id: String,
    pub name: String,
    pub gender: String,
    pub age: String,
    pub department: String,
    pub title: String,
    pub research: String,
}

#[derive(Serialize, Deserialize, Clone, Debug, Default)]
pub struct Book {
    pub isbn: String,
    pub title: String,
    pub author: String,
    pub publisher: String,
    pub year: String,
    pub category: String,
    pub quantity: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct Database {
    pub students: Vec<Student>,
    pub teachers: Vec<Teacher>,
    pub books: Vec<Book>,
    
    #[serde(skip)] // 不序列化到快照
    aof_file: Option<File>,
}

// 
//  数据库持久化（JSON 全量快照 + AOF 增量日志）
// 

impl Database {
    // ✅ 打开数据库并初始化AOF
    pub fn open(db_path: &str, aof_path: &str) -> Self {
        let mut db = Self::load(db_path);
        
        // 打开AOF文件
        if let Ok(file) = std::fs::OpenOptions::new()
            .create(true)
            .read(true)
            .append(true)
            .open(aof_path)
        {
            db.aof_file = Some(file);
            
            // 重放AOF日志恢复最新状态
            if let Ok(content) = std::fs::read_to_string(aof_path) {
                for line in content.lines() {
                    if let Ok(cmd) = serde_json::from_str::<Command>(line) {
                        db.apply_command(cmd);
                    }
                }
            }
        }
        
        db
    }

    // ✅ 应用AOF命令
    fn apply_command(&mut self, cmd: Command) {
        match cmd {
            Command::AddStudent(s) => {
                if !self.students.iter().any(|x| x.id == s.id) {
                    self.students.push(s);
                }
            }
            Command::UpdateStudent(idx, s) => {
                if idx < self.students.len() {
                    self.students[idx] = s;
                }
            }
            Command::DeleteStudent(idx) => {
                if idx < self.students.len() {
                    self.students.remove(idx);
                }
            }
            Command::AddTeacher(t) => {
                if !self.teachers.iter().any(|x| x.id == t.id) {
                    self.teachers.push(t);
                }
            }
            Command::UpdateTeacher(idx, t) => {
                if idx < self.teachers.len() {
                    self.teachers[idx] = t;
                }
            }
            Command::DeleteTeacher(idx) => {
                if idx < self.teachers.len() {
                    self.teachers.remove(idx);
                }
            }
            Command::AddBook(b) => {
                if !self.books.iter().any(|x| x.isbn == b.isbn) {
                    self.books.push(b);
                }
            }
            Command::UpdateBook(idx, b) => {
                if idx < self.books.len() {
                    self.books[idx] = b;
                }
            }
            Command::DeleteBook(idx) => {
                if idx < self.books.len() {
                    self.books.remove(idx);
                }
            }
        }
    }

    // ✅ 追加命令到AOF日志
    pub fn append_aof(&mut self, cmd: Command) -> Result<(), String> {
        if let Some(file) = &mut self.aof_file {
            let mut line = serde_json::to_string(&cmd).map_err(|e| e.to_string())?;
            line.push('\n');
            file.write_all(line.as_bytes()).map_err(|e| e.to_string())?;
            file.flush().map_err(|e| e.to_string())?;
        }
        Ok(())
    }

    // 加载快照
    pub fn load(path: &str) -> Self {
        if Path::new(path).exists() {
            std::fs::read_to_string(path)
                .ok()
                .and_then(|s| serde_json::from_str(&s).ok())
                .unwrap_or_default()
        } else {
            Self::default()
        }
    }

    // 保存快照
    pub fn save(&self, path: &str) -> Result<(), String> {
        let json = serde_json::to_string_pretty(self).map_err(|e| e.to_string())?;
        std::fs::write(path, json).map_err(|e| e.to_string())
    }
}


//  验证函数 — 返回 Err(提示信息)
pub fn validate_student(s: &Student) -> Result<(), String> {
    if s.id.trim().is_empty() {
        return Err("学号不能为空".into());
    }
    if s.id.len() != 7 || !s.id.chars().all(|c| c.is_ascii_digit()) {
        return Err("学号必须为 7 位纯数字，如: 2024001".into());
    }
    if s.name.trim().len() < 2 {
        return Err("姓名至少 2 个字符".into());
    }
    if s.gender.is_empty() {
        return Err("请选择性别".into());
    }
    let age: u32 = s.age.parse().map_err(|_| "年龄必须为整数".to_string())?;
    if age < 15 || age > 60 {
        return Err("年龄范围: 15 ~ 60".into());
    }
    if s.department.trim().is_empty() {
        return Err("院系不能为空".into());
    }
    if s.major.trim().is_empty() {
        return Err("专业不能为空".into());
    }
    if s.class_name.trim().is_empty() {
        return Err("班级不能为空".into());
    }
    Ok(())
}

pub fn validate_teacher(t: &Teacher) -> Result<(), String> {
    if t.id.len() != 7 {
        return Err("工号长度为 7 位 (T + 6 位数字)".into());
    }
    if !t.id.starts_with('T') || !t.id[1..].chars().all(|c| c.is_ascii_digit()) {
        return Err("工号格式: T + 6 位数字，如: T202401".into());
    }
    if t.name.trim().len() < 2 {
        return Err("姓名至少 2 个字符".into());
    }
    if t.gender.is_empty() {
        return Err("请选择性别".into());
    }
    let age: u32 = t.age.parse().map_err(|_| "年龄必须为整数".to_string())?;
    if age < 25 || age > 70 {
        return Err("年龄范围: 25 ~ 70".into());
    }
    if t.department.trim().is_empty() {
        return Err("院系不能为空".into());
    }
    if t.title.is_empty() {
        return Err("请选择职称".into());
    }
    if t.research.trim().is_empty() {
        return Err("研究方向不能为空".into());
    }
    Ok(())
}

pub fn validate_book(b: &Book) -> Result<(), String> {
    if b.isbn.len() != 13 || !b.isbn.chars().all(|c| c.is_ascii_digit()) {
        return Err("ISBN 必须为 13 位纯数字".into());
    }
    if b.title.trim().is_empty() {
        return Err("书名不能为空".into());
    }
    if b.author.trim().is_empty() {
        return Err("作者不能为空".into());
    }
    if b.publisher.trim().is_empty() {
        return Err("出版社不能为空".into());
    }
    let year: u32 = b.year.parse().map_err(|_| "年份必须为整数".to_string())?;
    if year < 1900 || year > 2099 {
        return Err("出版年份范围: 1900 ~ 2099".into());
    }
    if b.category.trim().is_empty() {
        return Err("分类不能为空".into());
    }
    let _: u32 = b
        .quantity
        .parse()
        .map_err(|_| "数量必须为非负整数".to_string())?;
    Ok(())
}

/// 计算学生平均成绩
pub fn student_avg_grade(s: &Student) -> Option<f64> {
    if s.grades.is_empty() {
        None
    } else {
        Some(s.grades.iter().map(|g| g.score).sum::<f64>() / s.grades.len() as f64)
    }
}