use std::collections::{HashMap, HashSet};

use eframe::egui;
use egui::{Align2, Color32, FontId, Rect, RichText, Stroke, Vec2};

use kvstore::{
    export_low_stock_report, export_student_report, student_avg_grade, validate_book,
    validate_student, validate_teacher, Book, Command, Database, Grade, Student, Teacher,
};

// 颜色 

const SIDEBAR_BG: Color32 = Color32::from_rgb(22, 27, 44);
const SIDEBAR_HOVER: Color32 = Color32::from_rgb(33, 42, 68);
const SIDEBAR_ACTIVE: Color32 = Color32::from_rgb(42, 55, 95);
const SIDEBAR_TEXT: Color32 = Color32::from_rgb(150, 160, 185);
const SIDEBAR_TEXT_BRIGHT: Color32 = Color32::from_rgb(220, 230, 255);
const CONTENT_BG: Color32 = Color32::from_rgb(243, 245, 250);
const CARD_BG: Color32 = Color32::from_rgb(255, 255, 255);
const CARD_BORDER: Color32 = Color32::from_rgb(225, 230, 240);
const ACCENT: Color32 = Color32::from_rgb(55, 100, 220);
const ACCENT_LIGHT: Color32 = Color32::from_rgb(235, 240, 255);
const SUCCESS: Color32 = Color32::from_rgb(46, 160, 67);
const DANGER: Color32 = Color32::from_rgb(220, 53, 69);
const TEXT_PRIMARY: Color32 = Color32::from_rgb(30, 35, 50);
const TEXT_HINT: Color32 = Color32::from_rgb(140, 150, 170);
const WARNING: Color32 = Color32::from_rgb(255, 152, 0);
const WARNING_BG: Color32 = Color32::from_rgb(255, 243, 224);
const PAGE_SIZE: usize = 50; // ✅ 分页：每页50条，支持大规模数据

//  标签页 & 排序 

#[derive(PartialEq, Clone, Copy)]
enum Tab {
    Students,
    Teachers,
    Books,
    Stats,
}

/// 学生列表排序方式
#[derive(PartialEq, Clone, Copy)]
enum StudentSort {
    Default,    // 原始录入顺序
    ByAvgGrade, // 均分降序 → 姓名升序
    ByClass,    // 班级升序 → 姓名升序
}

//  应用状态 

pub struct KvApp {
    db: Database,
    db_path: String,
    aof_path: String,
    active_tab: Tab,

    student_form: Student,
    student_edit_idx: Option<usize>,
    student_search: String,
    grade_course: String,
    grade_score: String,
    student_selected: HashSet<usize>,
    student_sort: StudentSort,
    student_page: usize, // ✅ 分页：学生当前页
    student_filtered_indices: Vec<usize>, // ✅ 缓存过滤结果索引（避免每帧全量克隆）
    student_filter_query: String, // ✅ 记录上次搜索词，用于判断缓存是否失效

    teacher_form: Teacher,
    teacher_edit_idx: Option<usize>,
    teacher_search: String,
    teacher_selected: HashSet<usize>,
    teacher_page: usize, // ✅ 分页：教师当前页
    teacher_filtered_indices: Vec<usize>,

    book_form: Book,
    book_edit_idx: Option<usize>,
    book_search: String,
    book_selected: HashSet<usize>,
    low_stock_threshold: u32, // ✅ 图书低库存预警阈值
    book_page: usize, // ✅ 分页：书籍当前页
    book_filtered_indices: Vec<usize>,

    status_msg: String,
    status_ok: bool,
}


//  初始化
impl KvApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        setup_fonts(&cc.egui_ctx);
        setup_theme(&cc.egui_ctx);
        
        // ✅ 使用open方法同时加载快照和AOF
        let db = Database::open("campus_db.json", "campus_db.aof");

        Self {
            db,
            db_path: "campus_db.json".into(),
            aof_path: "campus_db.aof".into(),
            active_tab: Tab::Students,
            student_form: Student::default(),
            student_edit_idx: None,
            student_search: String::new(),
            grade_course: String::new(),
            grade_score: String::new(),
            student_selected: HashSet::new(),
            student_sort: StudentSort::Default,
            student_page: 0,
            student_filtered_indices: Vec::new(),
            student_filter_query: String::new(),
            teacher_form: Teacher::default(),
            teacher_edit_idx: None,
            teacher_search: String::new(),
            teacher_selected: HashSet::new(),
            teacher_page: 0,
            teacher_filtered_indices: Vec::new(),
            book_form: Book::default(),
            book_edit_idx: None,
            book_search: String::new(),
            book_selected: HashSet::new(),
            low_stock_threshold: 3, // ✅ 默认预警阈值：库存 ≤ 3 本
            book_page: 0,
            book_filtered_indices: Vec::new(),
            status_msg: "✅ 系统启动成功，数据已加载".into(),
            status_ok: true,
        }
    }
}

impl eframe::App for KvApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.render_status_bar(ctx);
        self.render_sidebar(ctx);
        egui::CentralPanel::default()
            .frame(egui::Frame::none().fill(CONTENT_BG).inner_margin(16.0))
            .show(ctx, |ui| {
                egui::ScrollArea::vertical()
                    .auto_shrink([false, false])
                    .show(ui, |ui| match self.active_tab {
                        Tab::Students => self.render_student_tab(ui),
                        Tab::Teachers => self.render_teacher_tab(ui),
                        Tab::Books => self.render_book_tab(ui),
                        Tab::Stats => self.render_stats_tab(ui),
                    });
            });
    }
}



//  状态栏 & 侧边栏
impl KvApp {
    fn render_status_bar(&self, ctx: &egui::Context) {
        let bg = if self.status_ok {
            Color32::from_rgb(232, 245, 233)
        } else {
            Color32::from_rgb(255, 235, 238)
        };
        egui::TopBottomPanel::bottom("status_bar")
            .exact_height(30.0)
            .frame(egui::Frame::none().fill(bg).inner_margin(8.0))
            .show(ctx, |ui| {
                let color = if self.status_ok { SUCCESS } else { DANGER };
                ui.horizontal_centered(|ui| {
                    ui.label(RichText::new(&self.status_msg).color(color).size(12.5));
                });
            });
    }

    fn render_sidebar(&mut self, ctx: &egui::Context) {
        egui::SidePanel::left("sidebar")
            .resizable(false)
            .exact_width(200.0)
            .frame(egui::Frame::none().fill(SIDEBAR_BG))
            .show(ctx, |ui| {
                ui.add_space(28.0);
                ui.vertical_centered(|ui| {
                    ui.label(RichText::new("📚").size(30.0));
                    ui.add_space(4.0);
                    ui.label(
                        RichText::new("校园信息")
                            .size(18.0)
                            .color(SIDEBAR_TEXT_BRIGHT)
                            .strong(),
                    );
                    ui.label(
                        RichText::new("管理系统")
                            .size(18.0)
                            .color(SIDEBAR_TEXT_BRIGHT)
                            .strong(),
                    );
                });
                ui.add_space(28.0);
                ui.separator();
                ui.add_space(12.0);

                // ✅ 计算低库存书籍数量用于侧边栏警告
                let low_stock_count = self
                    .db
                    .books
                    .iter()
                    .filter(|b| {
                        b.quantity
                            .parse::<u32>()
                            .map(|q| q <= self.low_stock_threshold)
                            .unwrap_or(false)
                    })
                    .count();

                let tabs = [
                    (Tab::Students, "👤", "学生管理", self.db.students.len(), 0),
                    (Tab::Teachers, "👨‍🏫", "教师管理", self.db.teachers.len(), 0),
                    (Tab::Books, "📖", "书籍管理", self.db.books.len(), low_stock_count),
                    (Tab::Stats, "📊", "数据统计", 0, 0),
                ];
                for (tab, icon, label, count, warn_count) in tabs {
                    let active = self.active_tab == tab;
                    let text = if warn_count > 0 {
                        format!(
                            "{}  {} ({}) ⚠️{}",
                            icon, label, count, warn_count
                        )
                    } else if count > 0 {
                        format!("{}  {} ({})", icon, label, count)
                    } else {
                        format!("{}  {}", icon, label)
                    };
                    if nav_button(ui, &text, active) {
                        self.active_tab = tab;
                        self.clear_all_forms();
                    }
                    ui.add_space(4.0);
                }

                ui.with_layout(egui::Layout::bottom_up(egui::Align::Center), |ui| {
                    ui.add_space(12.0);
                    ui.label(RichText::new("v2.0.0").size(11.0).color(SIDEBAR_TEXT));
                    ui.add_space(10.0);
                    ui.separator();
                    ui.add_space(8.0);

                    // ✅ 清空全部数据
                    if ui.add(
                        egui::Button::new(RichText::new("🗑️ 清空全部数据").size(12.5).color(DANGER))
                            .fill(SIDEBAR_HOVER)
                            .rounding(6.0)
                            .min_size(Vec2::new(170.0, 32.0)),
                    ).clicked() {
                        self.db = Database::open(&self.db_path, &self.aof_path);
                        // 清空并重建
                        self.db.students.clear();
                        self.db.teachers.clear();
                        self.db.books.clear();
                        self.db.rebuild_indexes();
                        // 清空 AOF 文件
                        let _ = std::fs::write(&self.aof_path, "");
                        let _ = self.db.save(&self.db_path);
                        self.clear_all_forms();
                        self.status_msg = "✅ 已清空全部数据".into();
                        self.status_ok = true;
                    }
                    ui.add_space(4.0);

                    if ui.add(sidebar_action_btn("📤 导出全部数据")).clicked() {
                        if let Some(path) = rfd::FileDialog::new()
                            .add_filter("JSON", &["json"])
                            .save_file()
                        {
                            match self.db.save(path.to_str().unwrap_or("export.json")) {
                                Ok(()) => {
                                    self.status_msg = "✅ 数据导出成功".into();
                                    self.status_ok = true;
                                }
                                Err(e) => {
                                    self.status_msg = format!("❌ 导出失败: {}", e);
                                    self.status_ok = false;
                                }
                            }
                        }
                    }
                    ui.add_space(4.0);
                    
                    // ✅ 支持JSON/CSV导入
                    if ui.add(sidebar_action_btn("📥 导入数据 (JSON/CSV)")).clicked() {
                        if let Some(path) = rfd::FileDialog::new()
                            .add_filter("数据文件", &["json", "csv"])
                            .pick_file()
                        {
                            match std::fs::read_to_string(&path) {
                                Ok(content) => {
                                    let ext = path.extension().and_then(|s| s.to_str()).unwrap_or("").to_lowercase();
                                    match ext.as_str() {
                                        "json" => {
                                            match serde_json::from_str::<Database>(&content) {
                                                Ok(imported) => {
                                                    self.db = imported;
                                                    self.db.rebuild_indexes();
                                                    self.clear_all_forms();
                                                    let _ = self.db.save(&self.db_path);
                                                    self.status_msg = "✅ JSON数据导入成功".into();
                                                    self.status_ok = true;
                                                }
                                                Err(e) => {
                                                    self.status_msg = format!("❌ JSON解析失败: {}", e);
                                                    self.status_ok = false;
                                                }
                                            }
                                        }
                                        "csv" => {
                                            match self.import_from_csv(&content) {
                                                Ok((s, t, b)) => {
                                                    self.status_msg = format!("✅ CSV导入成功: 学生{}条, 教师{}条, 书籍{}条", s, t, b);
                                                    self.status_ok = true;
                                                }
                                                Err(e) => {
                                                    self.status_msg = format!("❌ CSV解析失败: {}", e);
                                                    self.status_ok = false;
                                                }
                                            }
                                        }
                                        _ => {
                                            self.status_msg = "❌ 不支持的文件格式".into();
                                            self.status_ok = false;
                                        }
                                    }
                                },
                                Err(e) => {
                                    self.status_msg = format!("❌ 读取失败: {}", e);
                                    self.status_ok = false;
                                }
                            }
                        }
                    }
                });
            });
    }

        // CSV导入函数
        fn import_from_csv(&mut self, content: &str) -> Result<(usize, usize, usize), String> {
            let mut rdr = csv::Reader::from_reader(content.as_bytes());
            let headers = rdr.headers().map_err(|e| e.to_string())?;
            
            let has_student_headers = headers.iter().any(|h| h == "学号") 
                && headers.iter().any(|h| h == "姓名") 
                && headers.iter().any(|h| h == "院系");
                
            let has_teacher_headers = headers.iter().any(|h| h == "工号") 
                && headers.iter().any(|h| h == "职称") 
                && headers.iter().any(|h| h == "研究方向");
                
            let has_book_headers = headers.iter().any(|h| h == "ISBN") 
                && headers.iter().any(|h| h == "书名") 
                && headers.iter().any(|h| h == "作者");

            if has_student_headers {
                // ✅ 学生CSV导入（支持成绩）
                use std::collections::HashMap;
                let mut student_map: HashMap<String, Student> = HashMap::new();
                
                for result in rdr.records() {
                    let record = result.map_err(|e| e.to_string())?;
                    if record.len() < 7 {
                        continue;
                    }
                    
                    let student_id = record.get(0).unwrap_or("").to_string();
                    if student_id.is_empty() {
                        continue;
                    }
                    
                    // 如果是新学生，先创建基本信息
                    let student = student_map.entry(student_id.clone()).or_insert_with(|| {
                        Student {
                            id: student_id.clone(),
                            name: record.get(1).unwrap_or("").to_string(),
                            gender: record.get(2).unwrap_or("").to_string(),
                            age: record.get(3).unwrap_or("").to_string(),
                            department: record.get(4).unwrap_or("").to_string(),
                            major: record.get(5).unwrap_or("").to_string(),
                            class_name: record.get(6).unwrap_or("").to_string(),
                            grades: Vec::new(),
                        }
                    });
                    
                    // ✅ 读取并添加成绩（如果有）
                    if record.len() >= 9 {
                        let course = record.get(7).unwrap_or("").trim().to_string();
                        let score_str = record.get(8).unwrap_or("").trim();
                        
                        if !course.is_empty() {
                            if let Ok(score) = score_str.parse::<f64>() {
                                if (0.0..=100.0).contains(&score) {
                                    student.grades.push(Grade { course, score });
                                }
                            }
                        }
                    }
                }
                
                // ✅ 验证并批量导入（修复 O(n²)→O(1) + 批量AOF）
                let mut count = 0;
                let mut aof_batch: Vec<Command> = Vec::with_capacity(student_map.len());
                self.db.students.reserve(student_map.len()); // 预分配，避免多次扩容
                for student in student_map.into_values() {
                    if validate_student(&student).is_ok()
                        && !self.db.student_index.contains_key(&student.id)
                    {
                        self.db
                            .student_index
                            .insert(student.id.clone(), self.db.students.len());
                        self.db.students.push(student.clone());
                        aof_batch.push(Command::AddStudent(student));
                        count += 1;
                    }
                }
                let _ = self.db.append_aof_batch(&aof_batch); // 仅一次批量刷盘
                
                let _ = self.db.save(&self.db_path);
                self.db.rebuild_indexes();
                self.student_filtered_indices.clear();
                self.student_filter_query.clear();
                Ok((count, 0, 0))
            } else if has_teacher_headers {
                // ✅ 教师CSV导入（批量优化：O(1)查重 + 预分配 + 批量AOF）
                let mut count = 0;
                let mut aof_batch: Vec<Command> = Vec::with_capacity(1024);
                self.db.teachers.reserve(1024);
                for result in rdr.records() {
                    let record = result.map_err(|e| e.to_string())?;
                    if record.len() < 7 {
                        continue;
                    }

                    let teacher = Teacher {
                        id: record.get(0).unwrap_or("").to_string(),
                        name: record.get(1).unwrap_or("").to_string(),
                        gender: record.get(2).unwrap_or("").to_string(),
                        age: record.get(3).unwrap_or("").to_string(),
                        department: record.get(4).unwrap_or("").to_string(),
                        title: record.get(5).unwrap_or("").to_string(),
                        research: record.get(6).unwrap_or("").to_string(),
                    };

                    if validate_teacher(&teacher).is_ok()
                        && !self.db.teacher_index.contains_key(&teacher.id)
                    {
                        self.db
                            .teacher_index
                            .insert(teacher.id.clone(), self.db.teachers.len());
                        self.db.teachers.push(teacher.clone());
                        aof_batch.push(Command::AddTeacher(teacher));
                        count += 1;
                    }
                }
                let _ = self.db.append_aof_batch(&aof_batch);
                
                let _ = self.db.save(&self.db_path);
                self.db.rebuild_indexes();
                self.teacher_filtered_indices.clear();
                Ok((0, count, 0))
            } else if has_book_headers {
                // ✅ 书籍CSV导入（批量优化：O(1)查重 + 预分配 + 批量AOF）
                let mut count = 0;
                let mut aof_batch: Vec<Command> = Vec::with_capacity(1024);
                self.db.books.reserve(1024);
                for result in rdr.records() {
                    let record = result.map_err(|e| e.to_string())?;
                    if record.len() < 7 {
                        continue;
                    }

                    let book = Book {
                        isbn: record.get(0).unwrap_or("").to_string(),
                        title: record.get(1).unwrap_or("").to_string(),
                        author: record.get(2).unwrap_or("").to_string(),
                        publisher: record.get(3).unwrap_or("").to_string(),
                        year: record.get(4).unwrap_or("").to_string(),
                        category: record.get(5).unwrap_or("").to_string(),
                        quantity: record.get(6).unwrap_or("").to_string(),
                    };

                    if validate_book(&book).is_ok()
                        && !self.db.book_index.contains_key(&book.isbn)
                    {
                        self.db
                            .book_index
                            .insert(book.isbn.clone(), self.db.books.len());
                        self.db.books.push(book.clone());
                        aof_batch.push(Command::AddBook(book));
                        count += 1;
                    }
                }
                let _ = self.db.append_aof_batch(&aof_batch);

                let _ = self.db.save(&self.db_path);
                self.db.rebuild_indexes();
                self.book_filtered_indices.clear();
                Ok((0, 0, count))
            } else {
                Err("无法识别CSV格式，请确保包含正确的表头".into())
            }
        }

    fn clear_all_forms(&mut self) {
        self.student_form = Student::default();
        self.student_edit_idx = None;
        self.grade_course.clear();
        self.grade_score.clear();
        self.student_selected.clear();
        self.student_sort = StudentSort::Default;
        self.student_page = 0;
        self.student_filtered_indices.clear();
        self.student_filter_query.clear();
        self.teacher_form = Teacher::default();
        self.teacher_edit_idx = None;
        self.teacher_selected.clear();
        self.teacher_page = 0;
        self.teacher_filtered_indices.clear();
        self.book_form = Book::default();
        self.book_edit_idx = None;
        self.book_selected.clear();
        self.book_page = 0;
        self.book_filtered_indices.clear();
    }
}

// 
//  学生管理
// 

impl KvApp {
    fn render_student_tab(&mut self, ui: &mut egui::Ui) {
        let is_edit = self.student_edit_idx.is_some();

        ui.label(
            RichText::new("👤 学生信息管理")
                .size(20.0)
                .strong()
                .color(TEXT_PRIMARY),
        );
        ui.add_space(2.0);
        ui.label(
            RichText::new("管理学生基本信息、院系专业及各科成绩")
                .size(13.0)
                .color(TEXT_HINT),
        );
        ui.add_space(12.0);

        //  表单卡片 
        card().show(ui, |ui| {
            let title = if is_edit {
                "✏️ 编辑学生信息"
            } else {
                "➕ 添加新学生"
            };
            ui.label(RichText::new(title).size(15.0).strong());
            ui.add_space(8.0);

            egui::Grid::new("student_form_grid")
                .num_columns(2)
                .spacing([16.0, 10.0])
                .show(ui, |ui| {
                    ui.label(RichText::new("学号:").strong());
                    ui.add_enabled(
                        !is_edit,
                        egui::TextEdit::singleline(&mut self.student_form.id)
                            .hint_text("7 位纯数字，如 2024001")
                            .desired_width(240.0),
                    );
                    ui.end_row();

                    ui.label(RichText::new("姓名:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.student_form.name)
                            .hint_text("2 ~ 20 个字符")
                            .desired_width(240.0),
                    );
                    ui.end_row();

                    ui.label(RichText::new("性别:").strong());
                    egui::ComboBox::from_id_salt("student_gender")
                        .selected_text(if self.student_form.gender.is_empty() {
                            "请选择"
                        } else {
                            &self.student_form.gender
                        })
                        .width(240.0)
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut self.student_form.gender, "男".into(), "男");
                            ui.selectable_value(&mut self.student_form.gender, "女".into(), "女");
                        });
                    ui.end_row();

                    ui.label(RichText::new("年龄:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.student_form.age)
                            .hint_text("15 ~ 60 的整数")
                            .desired_width(240.0),
                    );
                    ui.end_row();

                    ui.label(RichText::new("院系:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.student_form.department)
                            .hint_text("如: 计算机学院")
                            .desired_width(240.0),
                    );
                    ui.end_row();

                    ui.label(RichText::new("专业:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.student_form.major)
                            .hint_text("如: 软件工程")
                            .desired_width(240.0),
                    );
                    ui.end_row();

                    ui.label(RichText::new("班级:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.student_form.class_name)
                            .hint_text("如: 2024 级 1 班")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                });

            // 成绩管理
            ui.add_space(12.0);
            ui.separator();
            ui.add_space(8.0);
            ui.label(RichText::new("📝 成绩管理").size(14.0).strong());
            ui.add_space(6.0);

            ui.horizontal(|ui| {
                ui.label("课程:");
                ui.add(
                    egui::TextEdit::singleline(&mut self.grade_course)
                        .hint_text("课程名称")
                        .desired_width(120.0),
                );
                ui.label("分数:");
                ui.add(
                    egui::TextEdit::singleline(&mut self.grade_score)
                        .hint_text("0 ~ 100")
                        .desired_width(80.0),
                );
                if ui.button("➕ 添加").clicked() {
                    if self.grade_course.trim().is_empty() {
                        self.status_msg = "❌ 课程名不能为空".into();
                        self.status_ok = false;
                    } else {
                        match self.grade_score.parse::<f64>() {
                            Ok(s) if (0.0..=100.0).contains(&s) => {
                                self.student_form.grades.push(Grade {
                                    course: self.grade_course.clone(),
                                    score: s,
                                });
                                self.grade_course.clear();
                                self.grade_score.clear();
                            }
                            _ => {
                                self.status_msg = "❌ 分数须为 0 ~ 100 的数字".into();
                                self.status_ok = false;
                            }
                        }
                    }
                }
            });

            if !self.student_form.grades.is_empty() {
                ui.add_space(6.0);
                let mut rm: Option<usize> = None;
                egui::Grid::new("grade_list_grid")
                    .num_columns(3)
                    .spacing([24.0, 4.0])
                    .show(ui, |ui| {
                        ui.label(RichText::new("课程").strong().color(TEXT_HINT));
                        ui.label(RichText::new("分数").strong().color(TEXT_HINT));
                        ui.label(RichText::new("操作").strong().color(TEXT_HINT));
                        ui.end_row();
                        for (gi, g) in self.student_form.grades.iter().enumerate() {
                            ui.label(&g.course);
                            ui.label(format!("{:.1}", g.score));
                            if ui.small_button("✕").clicked() {
                                rm = Some(gi);
                            }
                            ui.end_row();
                        }
                    });
                if let Some(i) = rm {
                    self.student_form.grades.remove(i);
                }
            }

            // 操作按钮
            ui.add_space(14.0);
            ui.horizontal(|ui| {
                let label = if is_edit {
                    "💾 更新"
                } else {
                    "💾 保存"
                };
                if ui.button(RichText::new(label).strong()).clicked() {
                    if let Err(e) = validate_student(&self.student_form) {
                        self.status_msg = format!("❌ {}", e);
                        self.status_ok = false;
                    } else if !is_edit
                        && self
                            .db
                            .student_index
                            .contains_key(&self.student_form.id)
                    {
                        self.status_msg = "❌ 该学号已存在，请检查".into();
                        self.status_ok = false;
                    } else {
                        let name = self.student_form.name.clone();
                        if let Some(idx) = self.student_edit_idx {
                            self.db.students[idx] = self.student_form.clone();
                            let _ = self.db.append_aof(Command::UpdateStudent(idx, self.student_form.clone()));
                            self.status_msg = format!("✅ 已更新: {}", name);
                        } else {
                            self.db.students.push(self.student_form.clone());
                            let _ = self.db.append_aof(Command::AddStudent(self.student_form.clone()));
                            self.status_msg = format!("✅ 已添加: {}", name);
                        }
                        // ✅ 保存后重建索引并清空过滤缓存
                        self.db.rebuild_indexes();
                        self.student_filtered_indices.clear();
                        self.student_filter_query.clear();
                        let _ = self.db.save(&self.db_path);
                        self.status_ok = true;
                        self.student_form = Student::default();
                        self.student_edit_idx = None;
                        self.grade_course.clear();
                        self.grade_score.clear();
                    }
                }
                if is_edit && ui.button("取消").clicked() {
                    self.student_form = Student::default();
                    self.student_edit_idx = None;
                    self.grade_course.clear();
                    self.grade_score.clear();
                    self.status_msg = "已取消编辑".into();
                    self.status_ok = true;
                }
                if ui.button("🔄 重置").clicked() {
                    self.student_form = Student::default();
                    self.student_edit_idx = None;
                    self.grade_course.clear();
                    self.grade_score.clear();
                }
            });
        });

        ui.add_space(12.0);

        //  搜索 + 排序 
        ui.horizontal(|ui| {
            ui.label("🔍");
            ui.add(
                egui::TextEdit::singleline(&mut self.student_search)
                    .hint_text("输入学号、姓名、院系、专业等关键词搜索…")
                    .desired_width(300.0),
            );
            if ui.button("清空").clicked() {
                self.student_search.clear();
            }
            ui.separator();
            ui.label("排序:");
            for (sort_type, label) in [
                (StudentSort::Default, "录入顺序"),
                (StudentSort::ByAvgGrade, "按均分 ↓"),
                (StudentSort::ByClass, "按班级 ↑"),
            ] {
                if ui
                    .selectable_label(self.student_sort == sort_type, label)
                    .clicked()
                {
                    self.student_sort = sort_type;
                }
            }
        });

        // ✅ 大规模优化：只在搜索词变化或数据增减时重建过滤索引（避免每帧克隆全量数据）
        let query = self.student_search.to_lowercase();
        if self.student_filter_query != query
            || self.student_filtered_indices.is_empty()
            || self.student_filtered_indices.len() != self.db.students.len()
        {
            // 重建过滤索引
            self.student_filtered_indices.clear();
            if query.is_empty() {
                self.student_filtered_indices = (0..self.db.students.len()).collect();
            } else {
                self.student_filtered_indices = self
                    .db
                    .students
                    .iter()
                    .enumerate()
                    .filter(|(_, s)| {
                        s.id.to_lowercase().contains(&query)
                            || s.name.to_lowercase().contains(&query)
                            || s.department.to_lowercase().contains(&query)
                            || s.major.to_lowercase().contains(&query)
                            || s.class_name.to_lowercase().contains(&query)
                    })
                    .map(|(i, _)| i)
                    .collect();
            }
            self.student_filter_query = query;
            self.student_page = 0; // 搜索条件变，重置到第一页
        }

        // 排序索引（只在需要时排序）
        let mut sorted_indices = self.student_filtered_indices.clone();
        match self.student_sort {
            StudentSort::Default => {}
            StudentSort::ByAvgGrade => {
                sorted_indices.sort_by(|a, b| {
                    let avg_a = student_avg_grade(&self.db.students[*a]).unwrap_or(-1.0);
                    let avg_b = student_avg_grade(&self.db.students[*b]).unwrap_or(-1.0);
                    avg_b
                        .partial_cmp(&avg_a)
                        .unwrap()
                        .then(self.db.students[*a].name.cmp(&self.db.students[*b].name))
                });
            }
            StudentSort::ByClass => {
                sorted_indices.sort_by(|a, b| {
                    self.db.students[*a]
                        .class_name
                        .cmp(&self.db.students[*b].class_name)
                        .then(self.db.students[*a].name.cmp(&self.db.students[*b].name))
                });
            }
        }

        // ✅ 分页
        let total_filtered = sorted_indices.len();
        let total_pages = if total_filtered == 0 {
            0
        } else {
            (total_filtered + PAGE_SIZE - 1) / PAGE_SIZE
        };
        if self.student_page >= total_pages && total_pages > 0 {
            self.student_page = total_pages - 1;
        }
        let page_start = self.student_page * PAGE_SIZE;
        let page_end = ((self.student_page + 1) * PAGE_SIZE).min(total_filtered);
        let page_indices: Vec<usize> = sorted_indices[page_start..page_end].to_vec();

        // 收集选中快照（避免借用冲突）
        let selected_snap: HashSet<usize> = self.student_selected.iter().copied().collect();
        let mut checkbox_changes: Vec<(usize, bool)> = Vec::new();
        let mut batch_delete_flag = false;
        let mut edit_idx: Option<usize> = None;
        let mut del_idx: Option<usize> = None;
        let mut export_idx: Option<usize> = None;

        ui.add_space(8.0);

        card().show(ui, |ui| {
            ui.label(
                RichText::new(format!(
                    "📋 数据列表 (显示 {}/{} 条，第 {} / {} 页)",
                    page_indices.len(),
                    total_filtered,
                    self.student_page + 1,
                    if total_pages == 0 { 1 } else { total_pages }
                ))
                .size(14.0)
                .strong(),
            );
            ui.add_space(8.0);

            // 批量操作栏 + 分页控制
            ui.horizontal(|ui| {
                if ui.button("☑ 全选").clicked() {
                    for idx in &sorted_indices {
                        checkbox_changes.push((*idx, true));
                    }
                }
                if !selected_snap.is_empty() {
                    if ui.button("☐ 取消全选").clicked() {
                        for idx in &sorted_indices {
                            checkbox_changes.push((*idx, false));
                        }
                    }
                    if ui
                        .button(
                            RichText::new(format!("🗑️ 删除选中 ({})", selected_snap.len()))
                                .color(DANGER),
                        )
                        .clicked()
                    {
                        batch_delete_flag = true;
                    }
                }
                ui.separator();
                // 分页按钮
                if ui
                    .add_enabled(self.student_page > 0, egui::Button::new("◀ 上一页"))
                    .clicked()
                    && self.student_page > 0
                {
                    self.student_page -= 1;
                }
                ui.label(format!("{}/{}", self.student_page + 1, if total_pages == 0 { 1 } else { total_pages }));
                if ui
                    .add_enabled(
                        self.student_page + 1 < total_pages,
                        egui::Button::new("下一页 ▶"),
                    )
                    .clicked()
                    && self.student_page + 1 < total_pages
                {
                    self.student_page += 1;
                }
            });
            ui.add_space(6.0);

            if page_indices.is_empty() {
                ui.vertical_centered(|ui| {
                    ui.add_space(20.0);
                    ui.label(RichText::new("暂无数据").color(TEXT_HINT));
                });
                apply_student_actions(
                    self,
                    &checkbox_changes,
                    batch_delete_flag,
                    edit_idx,
                    del_idx,
                    export_idx,
                );
                return;
            }

            egui::ScrollArea::horizontal().show(ui, |ui| {
                egui::Grid::new("student_table")
                    .num_columns(11) // ✅ 增加一列：导出成绩单
                    .spacing([14.0, 8.0])
                    .striped(true)
                    .show(ui, |ui| {
                        ui.label("");
                        for h in &[
                            "学号",
                            "姓名",
                            "性别",
                            "年龄",
                            "院系",
                            "专业",
                            "班级",
                            "平均分",
                            "成绩单",
                            "操作",
                        ] {
                            ui.label(RichText::new(*h).strong().color(ACCENT));
                        }
                        ui.end_row();
                        for i in &page_indices {
                            let s = &self.db.students[*i];
                            let mut sel = selected_snap.contains(i);
                            ui.checkbox(&mut sel, "");
                            if sel != selected_snap.contains(i) {
                                checkbox_changes.push((*i, sel));
                            }
                            ui.label(&s.id);
                            ui.label(&s.name);
                            ui.label(&s.gender);
                            ui.label(&s.age);
                            ui.label(&s.department);
                            ui.label(&s.major);
                            ui.label(&s.class_name);
                            ui.label(match student_avg_grade(s) {
                                Some(a) => format!("{:.1}", a),
                                None => "—".into(),
                            });
                            // ✅ 成绩单导出按钮
                            if ui.small_button("📄").on_hover_text("导出成绩单").clicked() {
                                export_idx = Some(*i);
                            }
                            ui.horizontal(|ui| {
                                if ui.small_button("✏").clicked() {
                                    edit_idx = Some(*i);
                                }
                                if ui.small_button("🗑").clicked() {
                                    del_idx = Some(*i);
                                }
                            });
                            ui.end_row();
                        }
                    });
            });
        });

        apply_student_actions(
            self,
            &checkbox_changes,
            batch_delete_flag,
            edit_idx,
            del_idx,
            export_idx,
        );
    }
}

fn apply_student_actions(
    app: &mut KvApp,
    checkbox_changes: &[(usize, bool)],
    batch_delete: bool,
    edit_idx: Option<usize>,
    del_idx: Option<usize>,
    export_idx: Option<usize>,
) {
    for &(idx, selected) in checkbox_changes {
        if selected {
            app.student_selected.insert(idx);
        } else {
            app.student_selected.remove(&idx);
        }
    }
    if batch_delete && !app.student_selected.is_empty() {
        let count = app.student_selected.len();
        let mut indices: Vec<usize> = app.student_selected.drain().collect();
        indices.sort_unstable_by(|a, b| b.cmp(a));
        for idx in indices {
            if idx < app.db.students.len() {
                app.db.students.remove(idx);
                let _ = app.db.append_aof(Command::DeleteStudent(idx));
            }
        }
        let _ = app.db.save(&app.db_path);
        // 删除后重建索引和过滤缓存
        app.db.rebuild_indexes();
        app.student_filtered_indices.clear();
        app.student_filter_query.clear();
        app.status_msg = format!("✅ 已批量删除 {} 条学生记录", count);
        app.status_ok = true;
        return;
    }
    if let Some(i) = export_idx {
        if i < app.db.students.len() {
            let student = &app.db.students[i];
            let default_filename = format!("成绩单_{}_{}.csv", student.name, student.id);
            if let Some(path) = rfd::FileDialog::new()
                .set_file_name(&default_filename)
                .add_filter("CSV", &["csv"])
                .save_file()
            {
                let report = export_student_report(student);
                match std::fs::write(&path, &report) {
                    Ok(()) => {
                        app.status_msg = format!(
                            "✅ 成绩单已导出: {} ({})",
                            student.name, student.id
                        );
                        app.status_ok = true;
                    }
                    Err(e) => {
                        app.status_msg = format!("❌ 导出失败: {}", e);
                        app.status_ok = false;
                    }
                }
            }
        }
        return;
    }
    if let Some(i) = edit_idx {
        if i < app.db.students.len() {
            app.student_form = app.db.students[i].clone();
            app.student_edit_idx = Some(i);
            app.grade_course.clear();
            app.grade_score.clear();
            app.status_msg = format!("正在编辑: {}", app.db.students[i].name);
            app.status_ok = true;
        }
    }
    if let Some(i) = del_idx {
        if i < app.db.students.len() {
            let name = app.db.students[i].name.clone();
            app.db.students.remove(i);
            let _ = app.db.append_aof(Command::DeleteStudent(i));
            app.student_selected.remove(&i);
            let old = app.student_selected.drain().collect::<Vec<_>>();
            for idx in old {
                if idx > i {
                    app.student_selected.insert(idx - 1);
                } else {
                    app.student_selected.insert(idx);
                }
            }
            // ✅ 删除后重建索引和过滤缓存
            app.db.rebuild_indexes();
            app.student_filtered_indices.clear();
            app.student_filter_query.clear();
            let _ = app.db.save(&app.db_path);
            app.status_msg = format!("✅ 已删除: {}", name);
            app.status_ok = true;
            if app.student_edit_idx == Some(i) {
                app.student_form = Student::default();
                app.student_edit_idx = None;
            } else if let Some(ei) = app.student_edit_idx {
                if ei > i {
                    app.student_edit_idx = Some(ei - 1);
                }
            }
        }
    }
}

// 
//  教师管理
// 

impl KvApp {
    fn render_teacher_tab(&mut self, ui: &mut egui::Ui) {
        let is_edit = self.teacher_edit_idx.is_some();

        ui.label(
            RichText::new("👨‍🏫 教师信息管理")
                .size(20.0)
                .strong()
                .color(TEXT_PRIMARY),
        );
        ui.add_space(2.0);
        ui.label(
            RichText::new("管理教师基本信息、所属院系及研究方向")
                .size(13.0)
                .color(TEXT_HINT),
        );
        ui.add_space(12.0);

        //  表单卡片 
        card().show(ui, |ui| {
            let title = if is_edit {
                "✏️ 编辑教师信息"
            } else {
                "➕ 添加新教师"
            };
            ui.label(RichText::new(title).size(15.0).strong());
            ui.add_space(8.0);

            egui::Grid::new("teacher_form_grid")
                .num_columns(2)
                .spacing([16.0, 10.0])
                .show(ui, |ui| {
                    ui.label(RichText::new("工号:").strong());
                    ui.add_enabled(
                        !is_edit,
                        egui::TextEdit::singleline(&mut self.teacher_form.id)
                            .hint_text("格式: T + 6 位数字，如 T202401")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("姓名:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.teacher_form.name)
                            .hint_text("2 ~ 20 个字符")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("性别:").strong());
                    egui::ComboBox::from_id_salt("teacher_gender")
                        .selected_text(if self.teacher_form.gender.is_empty() {
                            "请选择"
                        } else {
                            &self.teacher_form.gender
                        })
                        .width(240.0)
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut self.teacher_form.gender, "男".into(), "男");
                            ui.selectable_value(&mut self.teacher_form.gender, "女".into(), "女");
                        });
                    ui.end_row();
                    ui.label(RichText::new("年龄:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.teacher_form.age)
                            .hint_text("25 ~ 70 的整数")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("院系:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.teacher_form.department)
                            .hint_text("如: 计算机学院")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("职称:").strong());
                    egui::ComboBox::from_id_salt("teacher_title")
                        .selected_text(if self.teacher_form.title.is_empty() {
                            "请选择职称"
                        } else {
                            &self.teacher_form.title
                        })
                        .width(240.0)
                        .show_ui(ui, |ui| {
                            for t in &["教授", "副教授", "讲师", "助教"] {
                                ui.selectable_value(
                                    &mut self.teacher_form.title,
                                    t.to_string(),
                                    *t,
                                );
                            }
                        });
                    ui.end_row();
                    ui.label(RichText::new("研究方向:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.teacher_form.research)
                            .hint_text("如: 人工智能、数据挖掘")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                });

            ui.add_space(14.0);
            ui.horizontal(|ui| {
                let label = if is_edit {
                    "💾 更新"
                } else {
                    "💾 保存"
                };
                if ui.button(RichText::new(label).strong()).clicked() {
                    if let Err(e) = validate_teacher(&self.teacher_form) {
                        self.status_msg = format!("❌ {}", e);
                        self.status_ok = false;
                    } else if !is_edit
                        && self
                            .db
                            .teacher_index
                            .contains_key(&self.teacher_form.id)
                    {
                        self.status_msg = "❌ 该工号已存在".into();
                        self.status_ok = false;
                    } else {
                        let name = self.teacher_form.name.clone();
                        if let Some(idx) = self.teacher_edit_idx {
                            self.db.teachers[idx] = self.teacher_form.clone();
                            let _ = self.db.append_aof(Command::UpdateTeacher(idx, self.teacher_form.clone()));
                            self.status_msg = format!("✅ 已更新: {}", name);
                        } else {
                            self.db.teachers.push(self.teacher_form.clone());
                            let _ = self.db.append_aof(Command::AddTeacher(self.teacher_form.clone()));
                            self.status_msg = format!("✅ 已添加: {}", name);
                        }
                        // ✅ 保存后重建索引并清空过滤缓存
                        self.db.rebuild_indexes();
                        self.teacher_filtered_indices.clear();
                        let _ = self.db.save(&self.db_path);
                        self.status_ok = true;
                        self.teacher_form = Teacher::default();
                        self.teacher_edit_idx = None;
                    }
                }
                if is_edit && ui.button("取消").clicked() {
                    self.teacher_form = Teacher::default();
                    self.teacher_edit_idx = None;
                    self.status_msg = "已取消编辑".into();
                    self.status_ok = true;
                }
                if ui.button("🔄 重置").clicked() {
                    self.teacher_form = Teacher::default();
                    self.teacher_edit_idx = None;
                }
            });
        });

        ui.add_space(12.0);
        ui.horizontal(|ui| {
            ui.label("🔍");
            ui.add(
                egui::TextEdit::singleline(&mut self.teacher_search)
                    .hint_text("输入工号、姓名、院系、职称等关键词搜索…")
                    .desired_width(360.0),
            );
            if ui.button("清空").clicked() {
                self.teacher_search.clear();
            }
        });

        let query = self.teacher_search.to_lowercase();
        // ✅ 大规模优化：缓存过滤索引
        self.teacher_filtered_indices.clear();
        if query.is_empty() {
            self.teacher_filtered_indices = (0..self.db.teachers.len()).collect();
        } else {
            self.teacher_filtered_indices = self
                .db
                .teachers
                .iter()
                .enumerate()
                .filter(|(_, t)| {
                    t.id.to_lowercase().contains(&query)
                        || t.name.to_lowercase().contains(&query)
                        || t.department.to_lowercase().contains(&query)
                        || t.title.to_lowercase().contains(&query)
                })
                .map(|(i, _)| i)
                .collect();
        }

        // ✅ 分页
        let total_filtered = self.teacher_filtered_indices.len();
        let total_pages = if total_filtered == 0 {
            0
        } else {
            (total_filtered + PAGE_SIZE - 1) / PAGE_SIZE
        };
        if self.teacher_page >= total_pages && total_pages > 0 {
            self.teacher_page = total_pages - 1;
        }
        let page_start = self.teacher_page * PAGE_SIZE;
        let page_end = ((self.teacher_page + 1) * PAGE_SIZE).min(total_filtered);
        let page_indices: Vec<usize> =
            self.teacher_filtered_indices[page_start..page_end].to_vec();

        let selected_snap: HashSet<usize> = self.teacher_selected.iter().copied().collect();
        let mut checkbox_changes: Vec<(usize, bool)> = Vec::new();
        let mut batch_delete_flag = false;
        let mut edit_idx: Option<usize> = None;
        let mut del_idx: Option<usize> = None;

        ui.add_space(8.0);

        card().show(ui, |ui| {
            ui.label(
                RichText::new(format!(
                    "📋 数据列表 (显示 {}/{} 条，第 {} / {} 页)",
                    page_indices.len(),
                    total_filtered,
                    self.teacher_page + 1,
                    if total_pages == 0 { 1 } else { total_pages }
                ))
                .size(14.0)
                .strong(),
            );
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                if ui.button("☑ 全选").clicked() {
                    for idx in &self.teacher_filtered_indices {
                        checkbox_changes.push((*idx, true));
                    }
                }
                if !selected_snap.is_empty() {
                    if ui.button("☐ 取消全选").clicked() {
                        for idx in &self.teacher_filtered_indices {
                            checkbox_changes.push((*idx, false));
                        }
                    }
                    if ui
                        .button(
                            RichText::new(format!("🗑️ 删除选中 ({})", selected_snap.len()))
                                .color(DANGER),
                        )
                        .clicked()
                    {
                        batch_delete_flag = true;
                    }
                }
                ui.separator();
                // 分页按钮
                if ui
                    .add_enabled(self.teacher_page > 0, egui::Button::new("◀ 上一页"))
                    .clicked()
                    && self.teacher_page > 0
                {
                    self.teacher_page -= 1;
                }
                ui.label(format!("{}/{}", self.teacher_page + 1, if total_pages == 0 { 1 } else { total_pages }));
                if ui
                    .add_enabled(
                        self.teacher_page + 1 < total_pages,
                        egui::Button::new("下一页 ▶"),
                    )
                    .clicked()
                    && self.teacher_page + 1 < total_pages
                {
                    self.teacher_page += 1;
                }
            });
            ui.add_space(6.0);
            if page_indices.is_empty() {
                ui.vertical_centered(|ui| {
                    ui.add_space(20.0);
                    ui.label(RichText::new("暂无数据").color(TEXT_HINT));
                });
                apply_teacher_actions(
                    self,
                    &checkbox_changes,
                    batch_delete_flag,
                    edit_idx,
                    del_idx,
                );
                return;
            }
            egui::ScrollArea::horizontal().show(ui, |ui| {
                egui::Grid::new("teacher_table")
                    .num_columns(9)
                    .spacing([14.0, 8.0])
                    .striped(true)
                    .show(ui, |ui| {
                        ui.label("");
                        for h in &[
                            "工号",
                            "姓名",
                            "性别",
                            "年龄",
                            "院系",
                            "职称",
                            "研究方向",
                            "操作",
                        ] {
                            ui.label(RichText::new(*h).strong().color(ACCENT));
                        }
                        ui.end_row();
                        for i in &page_indices {
                            let t = &self.db.teachers[*i];
                            let mut sel = selected_snap.contains(i);
                            ui.checkbox(&mut sel, "");
                            if sel != selected_snap.contains(i) {
                                checkbox_changes.push((*i, sel));
                            }
                            ui.label(&t.id);
                            ui.label(&t.name);
                            ui.label(&t.gender);
                            ui.label(&t.age);
                            ui.label(&t.department);
                            ui.label(&t.title);
                            ui.label(&t.research);
                            ui.horizontal(|ui| {
                                if ui.small_button("✏").clicked() {
                                    edit_idx = Some(*i);
                                }
                                if ui.small_button("🗑").clicked() {
                                    del_idx = Some(*i);
                                }
                            });
                            ui.end_row();
                        }
                    });
            });
        });
        apply_teacher_actions(
            self,
            &checkbox_changes,
            batch_delete_flag,
            edit_idx,
            del_idx,
        );
    }
}

fn apply_teacher_actions(
    app: &mut KvApp,
    checkbox_changes: &[(usize, bool)],
    batch_delete: bool,
    edit_idx: Option<usize>,
    del_idx: Option<usize>,
) {
    for &(idx, selected) in checkbox_changes {
        if selected {
            app.teacher_selected.insert(idx);
        } else {
            app.teacher_selected.remove(&idx);
        }
    }
    if batch_delete && !app.teacher_selected.is_empty() {
        let count = app.teacher_selected.len();
        let mut indices: Vec<usize> = app.teacher_selected.drain().collect();
        indices.sort_unstable_by(|a, b| b.cmp(a));
        for idx in indices {
            if idx < app.db.teachers.len() {
                app.db.teachers.remove(idx);
                let _ = app.db.append_aof(Command::DeleteTeacher(idx));
            }
        }
        let _ = app.db.save(&app.db_path);
        app.db.rebuild_indexes();
        app.teacher_filtered_indices.clear();
        app.status_msg = format!("✅ 已批量删除 {} 条教师记录", count);
        app.status_ok = true;
        return;
    }
    if let Some(i) = edit_idx {
        if i < app.db.teachers.len() {
            app.teacher_form = app.db.teachers[i].clone();
            app.teacher_edit_idx = Some(i);
            app.status_msg = format!("正在编辑: {}", app.db.teachers[i].name);
            app.status_ok = true;
        }
    }
    if let Some(i) = del_idx {
        if i < app.db.teachers.len() {
            let name = app.db.teachers[i].name.clone();
            app.db.teachers.remove(i);
            let _ = app.db.append_aof(Command::DeleteTeacher(i));
            app.teacher_selected.remove(&i);
            let old = app.teacher_selected.drain().collect::<Vec<_>>();
            for idx in old {
                if idx > i {
                    app.teacher_selected.insert(idx - 1);
                } else {
                    app.teacher_selected.insert(idx);
                }
            }
            app.db.rebuild_indexes();
            app.teacher_filtered_indices.clear();
            let _ = app.db.save(&app.db_path);
            app.status_msg = format!("✅ 已删除: {}", name);
            app.status_ok = true;
            if app.teacher_edit_idx == Some(i) {
                app.teacher_form = Teacher::default();
                app.teacher_edit_idx = None;
            } else if let Some(ei) = app.teacher_edit_idx {
                if ei > i {
                    app.teacher_edit_idx = Some(ei - 1);
                }
            }
        }
    }
}

// 
//  书籍管理
// 

impl KvApp {
    fn render_book_tab(&mut self, ui: &mut egui::Ui) {
        let is_edit = self.book_edit_idx.is_some();

        ui.label(
            RichText::new("📖 书籍信息管理")
                .size(20.0)
                .strong()
                .color(TEXT_PRIMARY),
        );
        ui.add_space(2.0);
        ui.label(
            RichText::new("管理图书馆藏书信息，包括 ISBN、作者、馆藏数量等")
                .size(13.0)
                .color(TEXT_HINT),
        );
        ui.add_space(12.0);

        //  表单卡片 
        card().show(ui, |ui| {
            let title = if is_edit {
                "✏️ 编辑书籍信息"
            } else {
                "➕ 添加新书籍"
            };
            ui.label(RichText::new(title).size(15.0).strong());
            ui.add_space(8.0);
            egui::Grid::new("book_form_grid")
                .num_columns(2)
                .spacing([16.0, 10.0])
                .show(ui, |ui| {
                    ui.label(RichText::new("ISBN:").strong());
                    ui.add_enabled(
                        !is_edit,
                        egui::TextEdit::singleline(&mut self.book_form.isbn)
                            .hint_text("13 位纯数字")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("书名:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.book_form.title)
                            .hint_text("如: 数据结构与算法")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("作者:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.book_form.author)
                            .hint_text("如: 严蔚敏")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("出版社:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.book_form.publisher)
                            .hint_text("如: 清华大学出版社")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("出版年份:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.book_form.year)
                            .hint_text("1900 ~ 2099 的整数")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("分类:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.book_form.category)
                            .hint_text("如: 计算机科学、文学")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                    ui.label(RichText::new("馆藏数量:").strong());
                    ui.add(
                        egui::TextEdit::singleline(&mut self.book_form.quantity)
                            .hint_text("非负整数，如 5")
                            .desired_width(240.0),
                    );
                    ui.end_row();
                });

            ui.add_space(14.0);
            ui.horizontal(|ui| {
                let label = if is_edit {
                    "💾 更新"
                } else {
                    "💾 保存"
                };
                if ui.button(RichText::new(label).strong()).clicked() {
                    if let Err(e) = validate_book(&self.book_form) {
                        self.status_msg = format!("❌ {}", e);
                        self.status_ok = false;
                    } else if !is_edit
                        && self.db.book_index.contains_key(&self.book_form.isbn)
                    {
                        self.status_msg = "❌ 该 ISBN 已存在".into();
                        self.status_ok = false;
                    } else {
                        let title = self.book_form.title.clone();
                        if let Some(idx) = self.book_edit_idx {
                            self.db.books[idx] = self.book_form.clone();
                            let _ = self.db.append_aof(Command::UpdateBook(idx, self.book_form.clone()));
                            self.status_msg = format!("✅ 已更新: {}", title);
                        } else {
                            self.db.books.push(self.book_form.clone());
                            let _ = self.db.append_aof(Command::AddBook(self.book_form.clone()));
                            self.status_msg = format!("✅ 已添加: {}", title);
                        }
                        // ✅ 保存后重建索引并清空过滤缓存
                        self.db.rebuild_indexes();
                        self.book_filtered_indices.clear();
                        let _ = self.db.save(&self.db_path);
                        self.status_ok = true;
                        self.book_form = Book::default();
                        self.book_edit_idx = None;
                    }
                }
                if is_edit && ui.button("取消").clicked() {
                    self.book_form = Book::default();
                    self.book_edit_idx = None;
                    self.status_msg = "已取消编辑".into();
                    self.status_ok = true;
                }
                if ui.button("🔄 重置").clicked() {
                    self.book_form = Book::default();
                    self.book_edit_idx = None;
                }
            });
        });

        ui.add_space(12.0);
        ui.horizontal(|ui| {
            ui.label("🔍");
            ui.add(
                egui::TextEdit::singleline(&mut self.book_search)
                    .hint_text("输入 ISBN、书名、作者、出版社、分类等关键词搜索…")
                    .desired_width(280.0),
            );
            if ui.button("清空").clicked() {
                self.book_search.clear();
            }
            ui.separator();
            // ✅ 低库存预警阈值设置
            ui.label("⚠️ 预警阈值:");
            ui.add(
                egui::DragValue::new(&mut self.low_stock_threshold)
                    .range(0..=100)
                    .speed(1)
                    .prefix("≤ "),
            );
            ui.label("本");
            if ui.small_button("重置").clicked() {
                self.low_stock_threshold = 3;
            }
        });

        let query = self.book_search.to_lowercase();
        // ✅ 大规模优化：缓存过滤索引
        self.book_filtered_indices.clear();
        if query.is_empty() {
            self.book_filtered_indices = (0..self.db.books.len()).collect();
        } else {
            self.book_filtered_indices = self
                .db
                .books
                .iter()
                .enumerate()
                .filter(|(_, b)| {
                    b.isbn.to_lowercase().contains(&query)
                        || b.title.to_lowercase().contains(&query)
                        || b.author.to_lowercase().contains(&query)
                        || b.publisher.to_lowercase().contains(&query)
                        || b.category.to_lowercase().contains(&query)
                })
                .map(|(i, _)| i)
                .collect();
        }

        // ✅ 分页
        let total_filtered = self.book_filtered_indices.len();
        let total_pages = if total_filtered == 0 {
            0
        } else {
            (total_filtered + PAGE_SIZE - 1) / PAGE_SIZE
        };
        if self.book_page >= total_pages && total_pages > 0 {
            self.book_page = total_pages - 1;
        }
        let page_start = self.book_page * PAGE_SIZE;
        let page_end = ((self.book_page + 1) * PAGE_SIZE).min(total_filtered);
        let page_indices: Vec<usize> =
            self.book_filtered_indices[page_start..page_end].to_vec();

        let selected_snap: HashSet<usize> = self.book_selected.iter().copied().collect();
        let mut checkbox_changes: Vec<(usize, bool)> = Vec::new();
        let mut batch_delete_flag = false;
        let mut edit_idx: Option<usize> = None;
        let mut del_idx: Option<usize> = None;

        // ✅ 低库存警告提示
        let low_stock_count = self
            .db
            .books
            .iter()
            .filter(|b| {
                b.quantity
                    .parse::<u32>()
                    .map(|q| q <= self.low_stock_threshold)
                    .unwrap_or(false)
            })
            .count();
        if low_stock_count > 0 {
            ui.add_space(6.0);
            ui.label(
                RichText::new(format!(
                    "⚠️ 警告: {} 种书籍库存 ≤ {} 本，急需补充！",
                    low_stock_count, self.low_stock_threshold
                ))
                .color(WARNING)
                .size(13.0),
            );
        }

        ui.add_space(8.0);
        card().show(ui, |ui| {
            ui.label(
                RichText::new(format!(
                    "📋 数据列表 (显示 {}/{} 条，第 {} / {} 页)",
                    page_indices.len(),
                    total_filtered,
                    self.book_page + 1,
                    if total_pages == 0 { 1 } else { total_pages }
                ))
                .size(14.0)
                .strong(),
            );
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                if ui.button("☑ 全选").clicked() {
                    for idx in &self.book_filtered_indices {
                        checkbox_changes.push((*idx, true));
                    }
                }
                if !selected_snap.is_empty() {
                    if ui.button("☐ 取消全选").clicked() {
                        for idx in &self.book_filtered_indices {
                            checkbox_changes.push((*idx, false));
                        }
                    }
                    if ui
                        .button(
                            RichText::new(format!("🗑️ 删除选中 ({})", selected_snap.len()))
                                .color(DANGER),
                        )
                        .clicked()
                    {
                        batch_delete_flag = true;
                    }
                }
                ui.separator();
                // 分页按钮
                if ui
                    .add_enabled(self.book_page > 0, egui::Button::new("◀ 上一页"))
                    .clicked()
                    && self.book_page > 0
                {
                    self.book_page -= 1;
                }
                ui.label(format!("{}/{}", self.book_page + 1, if total_pages == 0 { 1 } else { total_pages }));
                if ui
                    .add_enabled(
                        self.book_page + 1 < total_pages,
                        egui::Button::new("下一页 ▶"),
                    )
                    .clicked()
                    && self.book_page + 1 < total_pages
                {
                    self.book_page += 1;
                }
            });
            ui.add_space(6.0);
            if page_indices.is_empty() {
                ui.vertical_centered(|ui| {
                    ui.add_space(20.0);
                    ui.label(RichText::new("暂无数据").color(TEXT_HINT));
                });
                apply_book_actions(
                    self,
                    &checkbox_changes,
                    batch_delete_flag,
                    edit_idx,
                    del_idx,
                );
                return;
            }
            egui::ScrollArea::horizontal().show(ui, |ui| {
                egui::Grid::new("book_table")
                    .num_columns(10) // ✅ 增加一列：预警
                    .spacing([14.0, 8.0])
                    .striped(true)
                    .show(ui, |ui| {
                        ui.label("");
                        for h in &[
                            "ISBN",
                            "书名",
                            "作者",
                            "出版社",
                            "年份",
                            "分类",
                            "数量",
                            "预警",
                            "操作",
                        ] {
                            ui.label(RichText::new(*h).strong().color(ACCENT));
                        }
                        ui.end_row();
                        for i in &page_indices {
                            let b = &self.db.books[*i];
                            let mut sel = selected_snap.contains(i);
                            ui.checkbox(&mut sel, "");
                            if sel != selected_snap.contains(i) {
                                checkbox_changes.push((*i, sel));
                            }
                            ui.label(&b.isbn);
                            ui.label(&b.title);
                            ui.label(&b.author);
                            ui.label(&b.publisher);
                            ui.label(&b.year);
                            ui.label(&b.category);
                            // ✅ 低库存时用红色标注数量
                            let qty_parsed = b.quantity.parse::<u32>().unwrap_or(0);
                            if qty_parsed <= self.low_stock_threshold {
                                ui.label(
                                    RichText::new(&b.quantity)
                                        .color(WARNING)
                                        .strong(),
                                );
                            } else {
                                ui.label(&b.quantity);
                            }
                            // ✅ 预警列
                            if qty_parsed == 0 {
                                ui.label(RichText::new("🔴 缺货").color(DANGER).size(11.0));
                            } else if qty_parsed <= self.low_stock_threshold {
                                ui.label(
                                    RichText::new(format!("⚠️ 低库存({})", qty_parsed))
                                        .color(WARNING)
                                        .size(11.0),
                                );
                            } else {
                                ui.label(RichText::new("✅").color(SUCCESS).size(11.0));
                            }
                            ui.horizontal(|ui| {
                                if ui.small_button("✏").clicked() {
                                    edit_idx = Some(*i);
                                }
                                if ui.small_button("🗑").clicked() {
                                    del_idx = Some(*i);
                                }
                            });
                            ui.end_row();
                        }
                    });
            });
        });
        apply_book_actions(
            self,
            &checkbox_changes,
            batch_delete_flag,
            edit_idx,
            del_idx,
        );
    }
}

fn apply_book_actions(
    app: &mut KvApp,
    checkbox_changes: &[(usize, bool)],
    batch_delete: bool,
    edit_idx: Option<usize>,
    del_idx: Option<usize>,
) {
    for &(idx, selected) in checkbox_changes {
        if selected {
            app.book_selected.insert(idx);
        } else {
            app.book_selected.remove(&idx);
        }
    }
    if batch_delete && !app.book_selected.is_empty() {
        let count = app.book_selected.len();
        let mut indices: Vec<usize> = app.book_selected.drain().collect();
        indices.sort_unstable_by(|a, b| b.cmp(a));
        for idx in indices {
            if idx < app.db.books.len() {
                app.db.books.remove(idx);
                let _ = app.db.append_aof(Command::DeleteBook(idx));
            }
        }
        let _ = app.db.save(&app.db_path);
        app.db.rebuild_indexes();
        app.book_filtered_indices.clear();
        app.status_msg = format!("✅ 已批量删除 {} 条书籍记录", count);
        app.status_ok = true;
        return;
    }
    if let Some(i) = edit_idx {
        if i < app.db.books.len() {
            app.book_form = app.db.books[i].clone();
            app.book_edit_idx = Some(i);
            app.status_msg = format!("正在编辑: {}", app.db.books[i].title);
            app.status_ok = true;
        }
    }
    if let Some(i) = del_idx {
        if i < app.db.books.len() {
            let title = app.db.books[i].title.clone();
            app.db.books.remove(i);
            let _ = app.db.append_aof(Command::DeleteBook(i));
            app.book_selected.remove(&i);
            let old = app.book_selected.drain().collect::<Vec<_>>();
            for idx in old {
                if idx > i {
                    app.book_selected.insert(idx - 1);
                } else {
                    app.book_selected.insert(idx);
                }
            }
            app.db.rebuild_indexes();
            app.book_filtered_indices.clear();
            let _ = app.db.save(&app.db_path);
            app.status_msg = format!("✅ 已删除: {}", title);
            app.status_ok = true;
            if app.book_edit_idx == Some(i) {
                app.book_form = Book::default();
                app.book_edit_idx = None;
            } else if let Some(ei) = app.book_edit_idx {
                if ei > i {
                    app.book_edit_idx = Some(ei - 1);
                }
            }
        }
    }
}

// 
//  数据统计
// 

impl KvApp {
    fn render_stats_tab(&mut self, ui: &mut egui::Ui) {
        ui.label(
            RichText::new("📊 数据统计总览")
                .size(20.0)
                .strong()
                .color(TEXT_PRIMARY),
        );
        ui.add_space(2.0);
        ui.label(
            RichText::new("系统中所有实体的汇总统计信息")
                .size(13.0)
                .color(TEXT_HINT),
        );
        ui.add_space(16.0);

        let students = &self.db.students;
        let teachers = &self.db.teachers;
        let books = &self.db.books;

        //  概览卡片（统一高度）
        ui.columns(3, |cols| {
            let avg_str = if students.is_empty() {
                String::new()
            } else {
                let graded = students.iter().filter(|s| !s.grades.is_empty()).count();
                if graded == 0 {
                    String::new()
                } else {
                    let avg_all: f64 = students
                        .iter()
                        .filter_map(|s| student_avg_grade(s))
                        .sum::<f64>()
                        / graded as f64;
                    format!("全局平均分: {:.1}", avg_all)
                }
            };
            summary_card(
                &mut cols[0],
                "👤 学生",
                &format!("{} 人", students.len()),
                &avg_str,
            );
            summary_card(
                &mut cols[1],
                "👨‍🏫 教师",
                &format!("{} 人", teachers.len()),
                "",
            );
            let total_qty: u32 = books
                .iter()
                .filter_map(|b| b.quantity.parse::<u32>().ok())
                .sum();
            summary_card(
                &mut cols[2],
                "📖 书籍",
                &format!("{} 种 / {} 册", books.len(), total_qty),
                "",
            );
        });

        ui.add_space(16.0);

        //  学生分布
        if !students.is_empty() {
            card().show(ui, |ui| {
                ui.label(RichText::new("👤 学生分布").size(15.0).strong());
                ui.add_space(8.0);

                let male = students.iter().filter(|s| s.gender == "男").count();
                let female = students.len() - male;
                let total_s = students.len() as f32;
                ui.label(format!(
                    "男女比例: {} 男 : {} 女  (男 {:.1}% / 女 {:.1}%)",
                    male, female,
                    male as f32 / total_s * 100.0,
                    female as f32 / total_s * 100.0,
                ));

                let avg_age: f64 = students
                    .iter()
                    .filter_map(|s| s.age.parse::<f64>().ok())
                    .sum::<f64>()
                    / students.len() as f64;
                ui.label(format!("平均年龄: {:.1} 岁", avg_age));

                ui.add_space(8.0);
                ui.label(RichText::new("院系分布:").strong());
                ui.add_space(4.0);

                let mut dept_map: HashMap<&str, usize> = HashMap::new();
                for s in students {
                    *dept_map.entry(s.department.as_str()).or_insert(0) += 1;
                }
                let mut depts: Vec<_> = dept_map.into_iter().collect();
                depts.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(b.0)));
                let total = students.len() as f32;

                for (dept, cnt) in &depts {
                    let pct = *cnt as f32 / total * 100.0;
                    ui.label(
                        RichText::new(format!("{}: {} 人 ({:.0}%)", dept, cnt, pct))
                            .size(12.5)
                            .color(TEXT_PRIMARY),
                    );
                    ui.add_sized(
                        [ui.available_width(), 14.0],
                        egui::ProgressBar::new(*cnt as f32 / total),
                    );
                    ui.add_space(4.0);
                }

                // ✅ 院系 × 性别交叉统计
                ui.add_space(10.0);
                ui.separator();
                ui.add_space(6.0);
                ui.label(RichText::new("院系性别交叉统计:").size(13.0).strong());
                ui.add_space(4.0);

                let mut dept_gender: HashMap<&str, (usize, usize)> = HashMap::new();
                for s in students {
                    let entry = dept_gender.entry(s.department.as_str()).or_insert((0, 0));
                    if s.gender == "男" {
                        entry.0 += 1;
                    } else {
                        entry.1 += 1;
                    }
                }

                egui::Grid::new("dept_gender_grid")
                    .num_columns(5)
                    .spacing([14.0, 4.0])
                    .show(ui, |ui| {
                        ui.label(RichText::new("院系").strong().color(ACCENT));
                        ui.label(RichText::new("总人数").strong().color(ACCENT));
                        ui.label(RichText::new("男").strong().color(Color32::from_rgb(55, 120, 220)));
                        ui.label(RichText::new("女").strong().color(Color32::from_rgb(220, 80, 120)));
                        ui.label(RichText::new("男女比").strong().color(ACCENT));
                        ui.end_row();

                        let mut dg_sorted: Vec<_> = dept_gender.into_iter().collect();
                        dg_sorted.sort_by(|a, b| (b.1.0 + b.1.1).cmp(&(a.1.0 + a.1.1)));

                        for (dept, (m, f)) in &dg_sorted {
                            let total_d = m + f;
                            let ratio = if *f > 0 {
                                format!("{:.1}:1", *m as f32 / *f as f32)
                            } else {
                                "全男".into()
                            };
                            ui.label(*dept);
                            ui.label(format!("{}", total_d));
                            ui.label(format!("{}", m));
                            ui.label(format!("{}", f));
                            ui.label(RichText::new(ratio).size(11.0).color(TEXT_HINT));
                            ui.end_row();
                        }
                    });
            });
            ui.add_space(12.0);
        }

        //  教师分布 
        if !teachers.is_empty() {
            card().show(ui, |ui| {
                ui.label(RichText::new("👨‍🏫 教师职称分布").size(15.0).strong());
                ui.add_space(8.0);

                let mut title_map: HashMap<&str, usize> = HashMap::new();
                for t in teachers {
                    *title_map.entry(t.title.as_str()).or_insert(0) += 1;
                }
                let mut titles: Vec<_> = title_map.into_iter().collect();
                titles.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(b.0)));
                let total = teachers.len() as f32;

                for (title, cnt) in &titles {
                    let pct = *cnt as f32 / total * 100.0;
                    ui.label(
                        RichText::new(format!("{}: {} 人 ({:.0}%)", title, cnt, pct))
                            .size(12.5)
                            .color(TEXT_PRIMARY),
                    );
                    ui.add_sized(
                        [ui.available_width(), 14.0],
                        egui::ProgressBar::new(*cnt as f32 / total),
                    );
                    ui.add_space(4.0);
                }
            });
            ui.add_space(12.0);
        }

        //  书籍分布
        if !books.is_empty() {
            card().show(ui, |ui| {
                ui.label(RichText::new("📖 书籍分类分布").size(15.0).strong());
                ui.add_space(8.0);

                let mut cat_map: HashMap<&str, usize> = HashMap::new();
                for b in books {
                    *cat_map.entry(b.category.as_str()).or_insert(0) += 1;
                }
                let mut cats: Vec<_> = cat_map.into_iter().collect();
                cats.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(b.0)));
                let total = books.len() as f32;

                for (cat, cnt) in &cats {
                    let pct = *cnt as f32 / total * 100.0;
                    ui.label(
                        RichText::new(format!("{}: {} 种 ({:.0}%)", cat, cnt, pct))
                            .size(12.5)
                            .color(TEXT_PRIMARY),
                    );
                    ui.add_sized(
                        [ui.available_width(), 14.0],
                        egui::ProgressBar::new(*cnt as f32 / total),
                    );
                    ui.add_space(4.0);
                }
            });
            ui.add_space(12.0);

            // ✅ 低库存预警卡片
            let low_stock: Vec<(&Book, u32)> = self.db.get_low_stock_books(self.low_stock_threshold);
            if !low_stock.is_empty() {
                card().show(ui, |ui| {
                    ui.label(
                        RichText::new(format!(
                            "⚠️ 低库存预警 (阈值 ≤ {} 本，共 {} 种)",
                            self.low_stock_threshold,
                            low_stock.len()
                        ))
                        .size(15.0)
                        .strong()
                        .color(WARNING),
                    );
                    ui.add_space(8.0);

                    egui::ScrollArea::horizontal().show(ui, |ui| {
                        egui::Grid::new("low_stock_table")
                            .num_columns(5)
                            .spacing([14.0, 6.0])
                            .striped(true)
                            .show(ui, |ui| {
                                for h in &["ISBN", "书名", "作者", "分类", "库存"] {
                                    ui.label(RichText::new(*h).strong().color(ACCENT));
                                }
                                ui.end_row();
                                for (book, qty) in &low_stock {
                                    ui.label(&book.isbn);
                                    ui.label(&book.title);
                                    ui.label(&book.author);
                                    ui.label(&book.category);
                                    let color = if *qty == 0 {
                                        DANGER
                                    } else {
                                        WARNING
                                    };
                                    ui.label(
                                        RichText::new(format!("{} 本", qty)).color(color).strong(),
                                    );
                                    ui.end_row();
                                }
                            });
                    });

                    ui.add_space(8.0);
                    // ✅ 导出低库存报告按钮
                    if ui.button("📤 导出低库存报告").clicked() {
                        if let Some(path) = rfd::FileDialog::new()
                            .set_file_name("低库存预警报告.csv")
                            .add_filter("CSV", &["csv"])
                            .save_file()
                        {
                            let report = export_low_stock_report(&low_stock);
                            match std::fs::write(&path, &report) {
                                Ok(()) => {
                                    self.status_msg = format!(
                                        "✅ 低库存报告已导出: {} 种书籍",
                                        low_stock.len()
                                    );
                                    self.status_ok = true;
                                }
                                Err(e) => {
                                    self.status_msg = format!("❌ 导出失败: {}", e);
                                    self.status_ok = false;
                                }
                            }
                        }
                    }
                });
            }
        }

        if students.is_empty() && teachers.is_empty() && books.is_empty() {
            card().show(ui, |ui| {
                ui.vertical_centered(|ui| {
                    ui.add_space(40.0);
                    ui.label(RichText::new("📭 暂无数据").size(16.0).color(TEXT_HINT));
                    ui.add_space(8.0);
                    ui.label(
                        RichText::new("请先在各管理页面添加数据，或通过 JSON 文件导入")
                            .color(TEXT_HINT),
                    );
                    ui.add_space(40.0);
                });
            });
        }
    }
}

// 
//  辅助函数
// 

fn card() -> egui::Frame {
    egui::Frame::none()
        .fill(CARD_BG)
        .rounding(10.0)
        .stroke(Stroke::new(0.5, CARD_BORDER))
        .inner_margin(16.0)
}

fn nav_button(ui: &mut egui::Ui, text: &str, active: bool) -> bool {
    let desired = Vec2::new(ui.available_width(), 40.0);
    let (rect, resp) = ui.allocate_exact_size(desired, egui::Sense::click());
    if ui.is_rect_visible(rect) {
        let bg = if active {
            SIDEBAR_ACTIVE
        } else if resp.hovered() {
            SIDEBAR_HOVER
        } else {
            Color32::TRANSPARENT
        };
        ui.painter().rect_filled(rect, 6.0, bg);
        if active {
            let bar = Rect::from_min_size(
                rect.left_top() + Vec2::new(0.0, 8.0),
                Vec2::new(3.0, rect.height() - 16.0),
            );
            ui.painter().rect_filled(bar, 2.0, ACCENT);
        }
        let color = if active {
            SIDEBAR_TEXT_BRIGHT
        } else {
            SIDEBAR_TEXT
        };
        ui.painter().text(
            rect.left_center() + Vec2::new(18.0, 0.0),
            Align2::LEFT_CENTER,
            text,
            FontId::proportional(14.0),
            color,
        );
    }
    resp.clicked()
}

fn sidebar_action_btn(text: &str) -> egui::Button<'_> {
    egui::Button::new(RichText::new(text).size(12.5).color(SIDEBAR_TEXT_BRIGHT))
        .fill(SIDEBAR_HOVER)
        .rounding(6.0)
        .min_size(Vec2::new(170.0, 32.0))
}

fn summary_card(ui: &mut egui::Ui, icon: &str, value: &str, sub: &str) {
    card().show(ui, |ui| {
        ui.set_min_height(100.0);
        ui.vertical_centered(|ui| {
            ui.label(RichText::new(icon).size(24.0));
            ui.add_space(4.0);
            ui.label(RichText::new(value).size(20.0).strong().color(ACCENT));
            if !sub.is_empty() {
                ui.add_space(2.0);
                ui.label(RichText::new(sub).size(12.0).color(TEXT_HINT));
            }
        });
    });
}

// 
//  主题 & 字体
// 

fn setup_theme(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::light();
    visuals.override_text_color = Some(TEXT_PRIMARY);
    visuals.widgets.noninteractive.bg_fill = Color32::TRANSPARENT;
    visuals.widgets.inactive.bg_fill = Color32::from_rgb(248, 249, 252);
    visuals.widgets.inactive.bg_stroke = Stroke::new(1.0, Color32::from_rgb(210, 215, 225));
    visuals.widgets.hovered.bg_fill = ACCENT_LIGHT;
    visuals.widgets.active.bg_fill = Color32::from_rgb(210, 225, 255);
    visuals.selection.bg_fill = ACCENT;
    ctx.set_visuals(visuals);

    let mut style = (*ctx.style()).clone();
    style.spacing.item_spacing = Vec2::new(8.0, 6.0);
    style.spacing.button_padding = Vec2::new(10.0, 4.0);
    ctx.set_style(style);
}

fn setup_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();
    for path in &[
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/simhei.ttf",
    ] {
        if let Ok(data) = std::fs::read(path) {
            fonts
                .font_data
                .insert("chinese".into(), egui::FontData::from_owned(data));
            if let Some(f) = fonts.families.get_mut(&egui::FontFamily::Proportional) {
                f.insert(0, "chinese".into());
            }
            if let Some(f) = fonts.families.get_mut(&egui::FontFamily::Monospace) {
                f.insert(0, "chinese".into());
            }
            break;
        }
    }
    ctx.set_fonts(fonts);
}

// 
//  启动
// 

pub fn run_ui() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1060.0, 720.0])
            .with_min_inner_size([820.0, 560.0])
            .with_title("校园信息管理系统"),
        ..Default::default()
    };
    eframe::run_native(
        "校园信息管理系统",
        options,
        Box::new(|cc| Ok(Box::new(KvApp::new(cc)))),
    )
}