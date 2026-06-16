use serde::{Deserialize, Serialize};
use std::collections::HashMap;
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

/// 页码数据（用于分页）
#[derive(Debug, Clone, Default)]
pub struct PageInfo {
    pub page: usize,          // 当前页码 (0-based)
    pub page_size: usize,     // 每页条数
    pub total: usize,         // 总条数
}

impl PageInfo {
    pub fn total_pages(&self) -> usize {
        if self.page_size == 0 {
            return 0;
        }
        (self.total + self.page_size - 1) / self.page_size
    }

    pub fn has_prev(&self) -> bool {
        self.page > 0
    }

    pub fn has_next(&self) -> bool {
        self.page + 1 < self.total_pages()
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Database {
    pub students: Vec<Student>,
    pub teachers: Vec<Teacher>,
    pub books: Vec<Book>,

    #[serde(skip)]
    aof_file: Option<File>,

    // ✅ 大规模优化：HashMap 索引 — O(1) 按ID查找，避免 O(n) 线性扫描
    #[serde(skip)]
    pub student_index: HashMap<String, usize>,

    #[serde(skip)]
    pub teacher_index: HashMap<String, usize>,

    #[serde(skip)]
    pub book_index: HashMap<String, usize>,
}

impl Default for Database {
    fn default() -> Self {
        Self {
            students: Vec::new(),
            teachers: Vec::new(),
            books: Vec::new(),
            aof_file: None,
            student_index: HashMap::new(),
            teacher_index: HashMap::new(),
            book_index: HashMap::new(),
        }
    }
}

//
//  数据库持久化（JSON 全量快照 + AOF 增量日志）
//

impl Database {
    /// ✅ 打开数据库并初始化AOF，重建索引
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

        // ✅ 重建所有索引（加载快照 + AOF重放后统一重建）
        db.rebuild_indexes();

        db
    }

    /// ✅ 大规模优化：重建所有 HashMap 索引
    pub fn rebuild_indexes(&mut self) {
        // 预分配 HashMap 容量，减少 rehash
        self.student_index.clear();
        self.student_index.reserve(self.students.len());
        for (i, s) in self.students.iter().enumerate() {
            self.student_index.insert(s.id.clone(), i);
        }

        self.teacher_index.clear();
        self.teacher_index.reserve(self.teachers.len());
        for (i, t) in self.teachers.iter().enumerate() {
            self.teacher_index.insert(t.id.clone(), i);
        }

        self.book_index.clear();
        self.book_index.reserve(self.books.len());
        for (i, b) in self.books.iter().enumerate() {
            self.book_index.insert(b.isbn.clone(), i);
        }
    }

    /// ✅ 大规模优化：O(1) 按学号查找学生
    pub fn find_student_by_id(&self, id: &str) -> Option<&Student> {
        self.student_index
            .get(id)
            .and_then(|&i| self.students.get(i))
    }

    /// ✅ 大规模优化：O(1) 按工号查找教师
    pub fn find_teacher_by_id(&self, id: &str) -> Option<&Teacher> {
        self.teacher_index
            .get(id)
            .and_then(|&i| self.teachers.get(i))
    }

    /// ✅ 大规模优化：O(1) 按ISBN查找书籍
    pub fn find_book_by_isbn(&self, isbn: &str) -> Option<&Book> {
        self.book_index.get(isbn).and_then(|&i| self.books.get(i))
    }

    /// ✅ 图书低库存预警：返回低于阈值的书籍列表
    pub fn get_low_stock_books(&self, threshold: u32) -> Vec<(&Book, u32)> {
        let mut result = Vec::new();
        for b in &self.books {
            if let Ok(qty) = b.quantity.parse::<u32>() {
                if qty <= threshold {
                    result.push((b, qty));
                }
            }
        }
        result
    }

    // ✅ 应用AOF命令（同时维护索引）
    fn apply_command(&mut self, cmd: Command) {
        match cmd {
            Command::AddStudent(s) => {
                if !self.student_index.contains_key(&s.id) {
                    let idx = self.students.len();
                    self.students.push(s);
                    self.student_index
                        .insert(self.students[idx].id.clone(), idx);
                }
            }
            Command::UpdateStudent(idx, s) => {
                if idx < self.students.len() {
                    let old_id = self.students[idx].id.clone();
                    self.student_index.remove(&old_id);
                    self.student_index.insert(s.id.clone(), idx);
                    self.students[idx] = s;
                }
            }
            Command::DeleteStudent(idx) => {
                if idx < self.students.len() {
                    self.student_index.remove(&self.students[idx].id);
                    self.students.remove(idx);
                    // 移除后重建索引（后续索引位置需要调整）
                    self.rebuild_student_index();
                }
            }
            Command::AddTeacher(t) => {
                if !self.teacher_index.contains_key(&t.id) {
                    let idx = self.teachers.len();
                    self.teachers.push(t);
                    self.teacher_index
                        .insert(self.teachers[idx].id.clone(), idx);
                }
            }
            Command::UpdateTeacher(idx, t) => {
                if idx < self.teachers.len() {
                    let old_id = self.teachers[idx].id.clone();
                    self.teacher_index.remove(&old_id);
                    self.teacher_index.insert(t.id.clone(), idx);
                    self.teachers[idx] = t;
                }
            }
            Command::DeleteTeacher(idx) => {
                if idx < self.teachers.len() {
                    self.teacher_index.remove(&self.teachers[idx].id);
                    self.teachers.remove(idx);
                    self.rebuild_teacher_index();
                }
            }
            Command::AddBook(b) => {
                if !self.book_index.contains_key(&b.isbn) {
                    let idx = self.books.len();
                    self.books.push(b);
                    self.book_index
                        .insert(self.books[idx].isbn.clone(), idx);
                }
            }
            Command::UpdateBook(idx, b) => {
                if idx < self.books.len() {
                    let old_isbn = self.books[idx].isbn.clone();
                    self.book_index.remove(&old_isbn);
                    self.book_index.insert(b.isbn.clone(), idx);
                    self.books[idx] = b;
                }
            }
            Command::DeleteBook(idx) => {
                if idx < self.books.len() {
                    self.book_index.remove(&self.books[idx].isbn);
                    self.books.remove(idx);
                    self.rebuild_book_index();
                }
            }
        }
    }

    /// 删除后重建学生索引（O(n) — 仅在删除时调用）
    fn rebuild_student_index(&mut self) {
        self.student_index.clear();
        self.student_index.reserve(self.students.len());
        for (i, s) in self.students.iter().enumerate() {
            self.student_index.insert(s.id.clone(), i);
        }
    }

    fn rebuild_teacher_index(&mut self) {
        self.teacher_index.clear();
        self.teacher_index.reserve(self.teachers.len());
        for (i, t) in self.teachers.iter().enumerate() {
            self.teacher_index.insert(t.id.clone(), i);
        }
    }

    fn rebuild_book_index(&mut self) {
        self.book_index.clear();
        self.book_index.reserve(self.books.len());
        for (i, b) in self.books.iter().enumerate() {
            self.book_index.insert(b.isbn.clone(), i);
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
    let qty: u32 = b
        .quantity
        .parse()
        .map_err(|_| "数量必须为非负整数".to_string())?;
    if qty > 10000 {
        return Err("数量不能超过 10000".into());
    }
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

/// ✅ 生成单个学生成绩单 CSV 内容
pub fn export_student_report(student: &Student) -> String {
    let mut csv = String::new();
    // 表头信息
    csv.push_str("学号,姓名,性别,年龄,院系,专业,班级\n");
    csv.push_str(&format!(
        "{},{},{},{},{},{},{}\n\n",
        student.id,
        student.name,
        student.gender,
        student.age,
        student.department,
        student.major,
        student.class_name
    ));

    // 成绩列表
    csv.push_str("课程,分数,等级\n");
    if student.grades.is_empty() {
        csv.push_str("（暂无成绩）,,,\n");
    } else {
        for g in &student.grades {
            let grade_letter = match g.score {
                s if s >= 90.0 => "A (优秀)",
                s if s >= 80.0 => "B (良好)",
                s if s >= 70.0 => "C (中等)",
                s if s >= 60.0 => "D (及格)",
                _ => "F (不及格)",
            };
            csv.push_str(&format!("{},{:.1},{}\n", g.course, g.score, grade_letter));
        }
    }

    // 汇总
    csv.push('\n');
    csv.push_str(&format!(
        "课程总数,{},,\n",
        student.grades.len()
    ));
    match student_avg_grade(student) {
        Some(avg) => {
            csv.push_str(&format!("平均分,{:.2},,\n", avg));
            let overall = match avg {
                a if a >= 90.0 => "A (优秀)",
                a if a >= 80.0 => "B (良好)",
                a if a >= 70.0 => "C (中等)",
                a if a >= 60.0 => "D (及格)",
                _ => "F (不及格)",
            };
            csv.push_str(&format!("总评,{},,\n", overall));
        }
        None => {
            csv.push_str("平均分,N/A,,\n");
        }
    }

    csv
}

/// ✅ 生成图书低库存预警 CSV 内容
pub fn export_low_stock_report(books: &[(&Book, u32)]) -> String {
    let mut csv = String::new();
    csv.push_str("ISBN,书名,作者,出版社,分类,当前库存,预警阈值\n");
    for (book, qty) in books {
        csv.push_str(&format!(
            "{},{},{},{},{},{},{}\n",
            book.isbn, book.title, book.author, book.publisher, book.category, qty, "低库存"
        ));
    }
    csv
}
