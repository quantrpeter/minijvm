"""
minijvm 講解影片 — 用動畫拆解 ../src 入面嘅迷你 JVM（廣東話版）。

先生成旁白，再渲染（喺 video/ 目錄）：

    python3 gen_narration.py
    .venv/bin/manim render -qm minijvm_explainer.py MiniJVMExplainer

輸出喺 media/videos/minijvm_explainer/720p30/MiniJVMExplainer.mp4
"""

from pathlib import Path

import numpy as np
from manim import *
from pydub import AudioSegment

MONO = "Menlo"
NARR_DIR = Path(__file__).parent / "narration"

# 深藍色背景（唔用黑色）
config.background_color = "#141e33"

C_OPCODE = "#61afef"   # 藍   — opcode / 代碼
C_VALUE = "#e5c07b"    # 黃   — 執行期數值
C_STRING = "#98c379"   # 綠   — 字串 / 成功
C_ERR = "#e06c75"      # 紅   — 錯誤 / magic
C_ACCENT = "#c678dd"   # 紫   — 重點
C_DIM = "#6b7489"      # 灰   — 裝飾
TERM_BG = "#0b1220"    # 終端機背景


def mono(s, size=24, color=WHITE, **kw):
    return Text(s, font=MONO, font_size=size, color=color, **kw)


def sans(s, size=30, color=WHITE, **kw):
    return Text(s, font_size=size, color=color, **kw)


def bullets(items, size=28, buff=0.32, color=WHITE):
    rows = VGroup()
    for it in items:
        dot = Text("•", font_size=size, color=C_ACCENT)
        t = Text(it, font_size=size, color=color)
        rows.add(VGroup(dot, t).arrange(RIGHT, buff=0.25, aligned_edge=UP))
    rows.arrange(DOWN, aligned_edge=LEFT, buff=buff)
    return rows


def labelled_box(label, width, height, color=C_DIM, text_size=22, text_color=WHITE):
    box = RoundedRectangle(corner_radius=0.12, width=width, height=height,
                           stroke_color=color, stroke_width=3)
    txt = sans(label, size=text_size, color=text_color)
    if txt.width > width - 0.3:
        txt.scale_to_fit_width(width - 0.3)
    txt.move_to(box)
    return VGroup(box, txt)


class MiniJVMExplainer(Scene):

    # ---------- 基建 ----------

    def clear_scene(self):
        if self.mobjects:
            self.play(*[FadeOut(m) for m in self.mobjects], run_time=0.7)

    def section(self, index, title):
        banner = sans(f"{index}.  {title}", size=48, weight=BOLD, color=C_ACCENT)
        self.play(FadeIn(banner, scale=1.15))
        self.wait(1.4)
        header = sans(f"{index}. {title}", size=26, color=GREY_B).to_corner(UL, buff=0.4)
        self.play(Transform(banner, header), run_time=0.8)
        return banner

    def narr_start(self, name):
        """播放一段旁白，並記低佢應該幾時完。"""
        self._narr_end_time = None
        path = NARR_DIR / f"{name}.mp3"
        if path.exists():
            self.add_sound(str(path))
            dur = AudioSegment.from_file(str(path)).duration_seconds
            self._narr_end_time = self.renderer.time + dur

    def narr_end(self, tail=0.8):
        """如果旁白仲未講完，就等埋佢先過場。"""
        if getattr(self, "_narr_end_time", None):
            remaining = self._narr_end_time + tail - self.renderer.time
            if remaining > 0.05:
                self.wait(remaining)
        self._narr_end_time = None

    def construct(self):
        self.intro()
        self.pipeline()
        self.classfile_format()
        self.constant_pool()
        self.code_attribute()
        self.startup_flow()
        self.frame_anatomy()
        self.walkthrough_add()
        self.walkthrough_loop()
        self.method_calls()
        self.printing()
        self.opcode_summary()
        self.outro()

    # ---------- 0：片頭 ----------

    def intro(self):
        self.narr_start("intro")

        # 開場卡：Hong Kong Programming Society - Peter
        society = sans("Hong Kong Programming Society", 54, weight=BOLD, color=C_OPCODE)
        rule = Line(LEFT * 4.2, RIGHT * 4.2, stroke_color=C_DIM, stroke_width=2)
        presenter = sans("Peter", 40, color=C_VALUE)
        card = VGroup(society, rule, presenter).arrange(DOWN, buff=0.55)
        self.play(FadeIn(society, shift=UP * 0.3), run_time=1.2)
        self.play(Create(rule), FadeIn(presenter, shift=UP * 0.2))
        self.wait(2.2)
        self.play(FadeOut(card))

        title = sans("minijvm", size=100, weight=BOLD, color=C_OPCODE)
        sub = sans("用純 C99 寫嘅迷你 JVM", size=38, color=GREY_A)
        sub2 = sans("由 .class 原始位元組到執行 bytecode —— 完整拆解", size=28, color=GREY_B)
        group = VGroup(title, sub, sub2).arrange(DOWN, buff=0.5)
        self.play(Write(title), run_time=1.5)
        self.play(FadeIn(sub, shift=UP * 0.3))
        self.play(FadeIn(sub2, shift=UP * 0.3))
        self.wait(2)

        files = mono("src/classfile.c   src/interp.c   src/main.c", 26, GREY_B)
        files.next_to(group, DOWN, buff=0.8)
        note = sans("全部得 600 行 C。冇物件、冇 GC —— 淨係最核心嘅機制。",
                    size=26, color=C_VALUE).next_to(files, DOWN, buff=0.4)
        self.play(FadeIn(files), FadeIn(note))
        self.narr_end()
        self.clear_scene()

    # ---------- 1：成個流程 ----------

    def pipeline(self):
        self.narr_start("pipeline")
        self.section(1, "成個流程")

        java = labelled_box("Test.java", 2.3, 1.0, C_STRING, text_color=C_STRING)
        javac = labelled_box("javac", 1.7, 1.0, GREY_B)
        clazz = labelled_box("Test.class", 2.3, 1.0, C_VALUE, text_color=C_VALUE)
        mjvm = labelled_box("minijvm", 2.1, 1.0, C_OPCODE, text_color=C_OPCODE)
        out = labelled_box("輸出", 1.9, 1.0, C_STRING)

        row = VGroup(java, javac, clazz, mjvm, out).arrange(RIGHT, buff=0.85)
        row.move_to(UP * 1.6)
        arrows = VGroup(*[
            Arrow(row[i].get_right(), row[i + 1].get_left(), buff=0.08, stroke_width=4,
                  color=GREY_B)
            for i in range(4)
        ])

        self.play(LaggedStart(*[FadeIn(b, shift=UP * 0.2) for b in row], lag_ratio=0.2))
        self.play(LaggedStart(*[GrowArrow(a) for a in arrows], lag_ratio=0.2))

        cap1 = sans("javac 將 Java 原始碼編譯成二進制 .class 檔：", 26, GREY_A)
        cap2 = sans("入面裝住俾堆疊機執行嘅 bytecode。", 26, GREY_A)
        caps = VGroup(cap1, cap2).arrange(DOWN, buff=0.15).next_to(row, DOWN, buff=0.7)
        self.play(FadeIn(caps))
        self.wait(2)
        self.play(FadeOut(caps))

        # minijvm 拆開三個源碼檔
        brace = Brace(mjvm, DOWN, color=C_OPCODE)
        mods = VGroup(
            labelled_box("main.c — 入口：\n載入、搵 main、執行", 4.2, 1.1, GREY_B, 20),
            labelled_box("classfile.c — 解析器：\n位元組 → ClassFile 結構", 4.2, 1.1, C_VALUE, 20),
            labelled_box("interp.c — 解譯器：\nfetch / decode / execute", 4.2, 1.1, C_OPCODE, 20),
        ).arrange(RIGHT, buff=0.5).next_to(brace, DOWN, buff=0.5)
        self.play(GrowFromCenter(brace))
        self.play(LaggedStart(*[FadeIn(m, shift=DOWN * 0.2) for m in mods], lag_ratio=0.25))
        self.narr_end()
        self.clear_scene()

    # ---------- 2：.class 檔案結構 ----------

    def classfile_format(self):
        self.narr_start("classfile")
        self.section(2, ".class 檔案結構")

        intro = sans(".class 檔就係一串 big-endian 位元組，一氣呵成。", 28, GREY_A)
        intro.move_to(UP * 2.4)
        self.play(FadeIn(intro))

        segs = [
            ("magic", 1.5, C_ERR),
            ("version", 1.2, GREY_B),
            ("constant\npool", 2.4, C_VALUE),
            ("flags,\nthis, super", 1.8, GREY_B),
            ("interfaces", 1.5, GREY_B),
            ("fields", 1.1, GREY_B),
            ("methods\n(+ Code)", 2.2, C_OPCODE),
            ("attrs", 0.9, C_DIM),
        ]
        boxes = VGroup()
        for label, w, col in segs:
            r = Rectangle(width=w, height=1.1, stroke_color=col, stroke_width=3)
            t = sans(label, size=17, color=col)
            if t.width > w - 0.15:
                t.scale_to_fit_width(w - 0.15)
            boxes.add(VGroup(r, t.move_to(r)))
        boxes.arrange(RIGHT, buff=0).move_to(UP * 0.9)

        self.play(LaggedStart(*[Create(b[0]) for b in boxes], lag_ratio=0.12),
                  LaggedStart(*[FadeIn(b[1]) for b in boxes], lag_ratio=0.12))
        self.wait(1)

        # 讀取游標由左掃到右，好似 Reader struct 咁
        cursor = Triangle(color=C_ACCENT, fill_opacity=1).scale(0.16).rotate(PI)
        cursor.next_to(boxes[0], UP, buff=0.12).align_to(boxes[0], LEFT)
        cur_note = sans("classfile.c 用一個細細嘅游標（Reader），由頭讀到尾。",
                        24, C_ACCENT).next_to(boxes, DOWN, buff=0.5)
        self.play(FadeIn(cursor), FadeIn(cur_note))
        self.play(cursor.animate.align_to(boxes[-1], RIGHT), run_time=2.5, rate_func=linear)
        self.play(FadeOut(cursor), FadeOut(cur_note))

        # magic number
        magic = mono("CA FE BA BE", 30, C_ERR).next_to(boxes, DOWN, buff=0.8)
        magic_note = sans("頭 4 個位元組一定要係 0xCAFEBABE —— 唔係就唔係 class 檔。",
                          24, GREY_A).next_to(magic, DOWN, buff=0.3)
        self.play(Indicate(boxes[0], color=C_ERR), FadeIn(magic), FadeIn(magic_note))
        self.wait(2)
        self.play(FadeOut(magic), FadeOut(magic_note))

        notes = bullets([
            "常數池 —— 所有名稱、字串、數字同符號引用",
            "方法表 —— 每個方法帶住 Code 屬性，入面係 bytecode",
            "minijvm 用唔到嘅部分（欄位、大部分屬性）直接跳過",
        ], size=24)
        notes.next_to(boxes, DOWN, buff=0.7)
        self.play(Indicate(boxes[2], color=C_VALUE), Indicate(boxes[6], color=C_OPCODE))
        self.play(FadeIn(notes))
        self.narr_end()
        self.clear_scene()

    # ---------- 3：常數池 ----------

    def constant_pool(self):
        self.narr_start("constpool")
        self.section(3, "常數池")

        intro = sans("一個有編號嘅表 —— 其他所有嘢都指返入嚟。", 28, GREY_A)
        intro.move_to(UP * 2.6)
        self.play(FadeIn(intro))

        rows_src = [
            ("#1", "Methodref", "class #2, name&type #3", C_ACCENT),
            ("#2", "Class", "name #4", C_OPCODE),
            ("#3", "NameAndType", "name #5, type #6", C_OPCODE),
            ("#4", "Utf8", '"Test"', C_STRING),
            ("#5", "Utf8", '"add"', C_STRING),
            ("#6", "Utf8", '"(II)I"', C_STRING),
            ("#7", "String", "utf8 #8", C_VALUE),
            ("#8", "Utf8", '"hello, world"', C_STRING),
            ("#9", "Integer", "123456", C_VALUE),
        ]
        table = VGroup()
        for idx, tag, detail, col in rows_src:
            r = VGroup(
                mono(idx, 22, GREY_B),
                mono(tag.ljust(12), 22, col),
                mono(detail, 22, GREY_A),
            ).arrange(RIGHT, buff=0.45)
            table.add(r)
        table.arrange(DOWN, aligned_edge=LEFT, buff=0.18)
        table.next_to(intro, DOWN, buff=0.45).to_edge(LEFT, buff=0.9)

        self.play(LaggedStart(*[FadeIn(r, shift=RIGHT * 0.2) for r in table],
                              lag_ratio=0.1))
        self.wait(1)

        # 好似 cf_resolve_ref 咁解析 #1 嘅 Methodref
        q = sans("#1 嘅 Methodref 即係咩？", 26, C_ACCENT)
        q.to_edge(RIGHT, buff=0.7).shift(UP * 1.4)
        self.play(FadeIn(q), Circumscribe(table[0], color=C_ACCENT))

        def hop(frm, to, col):
            return CurvedArrow(table[frm].get_right() + RIGHT * 0.15,
                               table[to].get_right() + RIGHT * 0.15,
                               angle=-TAU / 5, color=col, stroke_width=3,
                               tip_length=0.18)

        a1 = hop(0, 1, C_OPCODE)
        a2 = hop(1, 3, C_STRING)
        a3 = hop(0, 2, C_OPCODE)
        a4 = hop(2, 4, C_STRING)
        a5 = hop(2, 5, C_STRING)

        self.play(Create(a1))
        self.play(Create(a2))
        self.play(Create(a3))
        self.play(Create(a4), Create(a5))

        result = mono('Test.add(II)I', 30, C_STRING)
        result_note = sans("類別 . 名稱 描述符 —— 跟住索引一路追出嚟\n（classfile.c 嘅 cf_resolve_ref）",
                           22, GREY_A)
        res = VGroup(result, result_note).arrange(DOWN, buff=0.3)
        res.to_edge(RIGHT, buff=0.7).shift(DOWN * 0.9)
        self.play(FadeIn(res, shift=UP * 0.3))
        self.narr_end()
        self.clear_scene()

    # ---------- 4：Code 屬性 ----------

    def code_attribute(self):
        self.narr_start("codeattr")
        self.section(4, "方法自己帶住 bytecode：Code 屬性")

        src = mono("static int add(int a, int b) { return a + b; }", 24, C_STRING)
        src.move_to(UP * 2.3)
        self.play(FadeIn(src))

        fields = VGroup(
            mono("u2 max_stack   = 2", 24, GREY_A),
            mono("u2 max_locals  = 2", 24, GREY_A),
            mono("u4 code_length = 4", 24, GREY_A),
            mono("u1 code[]      = 1A 1B 60 AC", 24, C_VALUE),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.25)
        frame_box = SurroundingRectangle(fields, color=C_OPCODE, buff=0.35, corner_radius=0.1)
        cap = sans("add 嘅 Code 屬性", 22, C_OPCODE).next_to(frame_box, UP, buff=0.15)
        code_grp = VGroup(fields, frame_box, cap).move_to(LEFT * 3.2 + DOWN * 0.4)

        self.play(Create(frame_box), FadeIn(cap))
        self.play(LaggedStart(*[FadeIn(f, shift=RIGHT * 0.2) for f in fields], lag_ratio=0.2))
        self.wait(1)

        # 解碼嗰四個位元組
        decode = VGroup(
            mono("1A  iload_0    ", 24, C_OPCODE),
            mono("1B  iload_1    ", 24, C_OPCODE),
            mono("60  iadd       ", 24, C_OPCODE),
            mono("AC  ireturn    ", 24, C_OPCODE),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.25)
        decode.move_to(RIGHT * 3.4 + DOWN * 0.4)
        arrow = Arrow(frame_box.get_right(), decode.get_left(), buff=0.25,
                      color=GREY_B, stroke_width=4)

        self.play(GrowArrow(arrow))
        self.play(LaggedStart(*[FadeIn(d, shift=RIGHT * 0.2) for d in decode], lag_ratio=0.2))

        note = sans("一個位元組一個 opcode。成個方法體得 4 個位元組。",
                    26, GREY_A).to_edge(DOWN, buff=0.8)
        self.play(FadeIn(note))
        self.narr_end()
        self.clear_scene()

    # ---------- 5：啟動 ----------

    def startup_flow(self):
        self.narr_start("startup")
        self.section(5, "啟動：main.c")

        steps = VGroup(
            labelled_box('classfile_load("Test.class")\n解析位元組 → ClassFile', 5.6, 1.2, C_VALUE, 22),
            labelled_box('cf_find_method("main",\n"([Ljava/lang/String;)V")', 5.6, 1.2, C_OPCODE, 22),
            labelled_box("interp_run(cf, main, NULL, 0)\n開始解譯", 5.6, 1.2, C_STRING, 22),
        ).arrange(DOWN, buff=0.75).shift(DOWN * 0.3)
        arrows = VGroup(*[
            Arrow(steps[i].get_bottom(), steps[i + 1].get_top(), buff=0.1,
                  color=GREY_B, stroke_width=4)
            for i in range(2)
        ])

        for i, s in enumerate(steps):
            self.play(FadeIn(s, shift=UP * 0.2))
            if i < 2:
                self.play(GrowArrow(arrows[i]), run_time=0.5)
        self.wait(1)

        note = sans("成個 driver 就係咁 —— 28 行。重頭戲全部喺 interp_run。",
                    26, GREY_A).to_edge(DOWN, buff=0.6)
        self.play(FadeIn(note))
        self.narr_end()
        self.clear_scene()

    # ---------- 6：frame 結構 ----------

    def frame_anatomy(self):
        self.narr_start("frame")
        self.section(6, "解譯器內部：一個方法一個 frame")

        struct = VGroup(
            mono("typedef struct {", 22, GREY_A),
            mono("    Slot *locals;  // 參數 + 局部變數", 22, C_VALUE),
            mono("    Slot *stack;   // 操作數堆疊", 22, C_OPCODE),
            mono("    int   sp;      // 堆疊指標", 22, GREY_A),
            mono("    uint32_t pc;   // 程式計數器", 22, C_ACCENT),
            mono("} Frame;", 22, GREY_A),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.16)
        struct.scale(0.92).to_edge(LEFT, buff=0.55).shift(UP * 0.9)
        self.play(LaggedStart(*[FadeIn(l, shift=RIGHT * 0.2) for l in struct], lag_ratio=0.12))

        note = sans("JVM 係一部堆疊機：\n冇暫存器 —— 每條指令\n都係推入／彈出操作數堆疊。",
                    24, GREY_A).next_to(struct, DOWN, buff=0.6, aligned_edge=LEFT)
        self.play(FadeIn(note))
        self.wait(1.5)

        # fetch-decode-execute 循環
        fetch = labelled_box("FETCH\nop = code[pc++]", 3.3, 1.15, C_ACCENT, 22)
        decode = labelled_box("DECODE\nswitch (op)", 3.3, 1.15, C_OPCODE, 22)
        execute = labelled_box("EXECUTE\npush / pop / jump / call", 3.6, 1.15, C_VALUE, 20)
        fetch.move_to(RIGHT * 3.6 + UP * 1.9)
        decode.move_to(RIGHT * 3.6 + UP * 0.1)
        execute.move_to(RIGHT * 3.6 + DOWN * 1.7)

        a1 = Arrow(fetch.get_bottom(), decode.get_top(), buff=0.1, color=GREY_B, stroke_width=4)
        a2 = Arrow(decode.get_bottom(), execute.get_top(), buff=0.1, color=GREY_B, stroke_width=4)
        a3 = CurvedArrow(execute.get_right(), fetch.get_right(), angle=-PI * 0.75,
                         color=GREY_B, stroke_width=3, tip_length=0.2)

        self.play(FadeIn(fetch, shift=UP * 0.2))
        self.play(GrowArrow(a1), FadeIn(decode, shift=UP * 0.2))
        self.play(GrowArrow(a2), FadeIn(execute, shift=UP * 0.2))
        self.play(Create(a3))
        loop_note = sans("interp.c：一個 for(;;) 迴圈，\n一個 opcode 一個 case",
                         22, GREY_B).next_to(execute, DOWN, buff=0.35)
        self.play(FadeIn(loop_note))
        self.narr_end()
        self.clear_scene()

    # ---------- 7：實戰執行 add ----------

    def walkthrough_add(self):
        self.narr_start("addwalk")
        self.section(7, "實戰執行 bytecode：add(2, 3)")

        # bytecode 列表（左邊）
        lines = VGroup(
            mono("0: iload_0", 26, C_OPCODE),
            mono("1: iload_1", 26, C_OPCODE),
            mono("2: iadd", 26, C_OPCODE),
            mono("3: ireturn", 26, C_OPCODE),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.4)
        lines.to_edge(LEFT, buff=1.2).shift(DOWN * 0.5)
        code_cap = sans("bytecode", 24, GREY_B).next_to(lines, UP, buff=0.4)

        # locals（中間）
        lslots = VGroup(*[Square(0.95, stroke_color=C_VALUE, stroke_width=3) for _ in range(2)])
        lslots.arrange(RIGHT, buff=0)
        lslots.move_to(DOWN * 1.8 + LEFT * 0.2)
        lidx = VGroup(*[mono(str(i), 18, GREY_B).next_to(lslots[i], DOWN, buff=0.12)
                        for i in range(2)])
        lcap = sans("locals（局部變數）", 24, C_VALUE).next_to(lslots, UP, buff=0.25)
        lvals = VGroup(mono("2", 30, C_VALUE).move_to(lslots[0]),
                       mono("3", 30, C_VALUE).move_to(lslots[1]))

        # 操作數堆疊（右邊）
        sslots = VGroup(*[Square(0.95, stroke_color=C_OPCODE, stroke_width=3) for _ in range(2)])
        sslots.arrange(UP, buff=0)
        sslots.move_to(RIGHT * 3.3 + DOWN * 0.9)
        scap = sans("操作數堆疊", 24, C_OPCODE).next_to(sslots, UP, buff=0.3)

        self.play(FadeIn(code_cap), FadeIn(lines),
                  Create(lslots), FadeIn(lidx), FadeIn(lcap),
                  Create(sslots), FadeIn(scap))
        arrived = sans("參數一開始擺喺 locals[0..1]", 22, GREY_A)
        arrived.next_to(lslots, DOWN, buff=0.55)
        self.play(FadeIn(lvals), FadeIn(arrived))
        self.wait(1.5)
        self.play(FadeOut(arrived))

        hl = SurroundingRectangle(lines[0], color=YELLOW, buff=0.08)
        self.play(Create(hl))

        stack = []

        def push_from(src_mobj, text, run_time=0.8):
            v = mono(text, 30, C_VALUE).move_to(src_mobj)
            target = sslots[len(stack)].get_center()
            self.add(v)
            self.play(v.animate.move_to(target), run_time=run_time)
            stack.append(v)

        # iload_0：locals[0] -> 堆疊
        push_from(lvals[0], "2")
        self.wait(0.4)

        # iload_1
        self.play(Transform(hl, SurroundingRectangle(lines[1], color=YELLOW, buff=0.08)))
        push_from(lvals[1], "3")
        self.wait(0.4)

        # iadd：彈出 3 同 2，推入 5
        self.play(Transform(hl, SurroundingRectangle(lines[2], color=YELLOW, buff=0.08)))
        b, a = stack.pop(), stack.pop()
        expr = mono("2 + 3", 28, C_VALUE).move_to(RIGHT * 5.3 + DOWN * 0.9)
        self.play(a.animate.move_to(expr.get_center() + LEFT * 0.05),
                  b.animate.move_to(expr.get_center() + RIGHT * 0.05))
        self.remove(a, b)
        result = mono("5", 30, C_STRING).move_to(expr)
        self.play(FadeIn(result, scale=1.3))
        self.play(result.animate.move_to(sslots[0]))
        stack.append(result)
        self.wait(0.6)

        # ireturn
        self.play(Transform(hl, SurroundingRectangle(lines[3], color=YELLOW, buff=0.08)))
        ret = sans("ireturn 彈出堆疊頂 —— 呢個就係回傳值", 24, C_STRING)
        ret.to_edge(DOWN, buff=0.55)
        self.play(FadeIn(ret), Indicate(stack[-1], color=C_STRING))
        self.narr_end()
        self.clear_scene()

    # ---------- 8：跳轉同迴圈 ----------

    def walkthrough_loop(self):
        self.narr_start("loop")
        self.section(8, "跳轉同迴圈")

        src = mono("int sum = 0; for (int i = 0; i < 3; i++) sum += i;", 22, C_STRING)
        src.move_to(UP * 2.5)
        self.play(FadeIn(src))

        listing = [
            (" 0: iconst_0", GREY_A),
            (" 1: istore_0        // sum = 0", GREY_A),
            (" 2: iconst_0", GREY_A),
            (" 3: istore_1        // i = 0", GREY_A),
            (" 4: iload_1", C_OPCODE),
            (" 5: iconst_3", C_OPCODE),
            (" 6: if_icmpge  17   // i >= 3 ? 走人", C_ACCENT),
            (" 9: iload_0", C_OPCODE),
            ("10: iload_1", C_OPCODE),
            ("11: iadd", C_OPCODE),
            ("12: istore_0        // sum += i", C_OPCODE),
            ("13: iinc 1, 1       // i++", C_OPCODE),
            ("16: goto 4          // 跳返上去", C_ACCENT),
            ("17: return", GREY_A),
        ]
        lines = VGroup(*[mono(t, 20, c) for t, c in listing])
        lines.arrange(DOWN, aligned_edge=LEFT, buff=0.11)
        lines.to_edge(LEFT, buff=1.1).shift(DOWN * 0.55)
        self.play(LaggedStart(*[FadeIn(l, shift=RIGHT * 0.15) for l in lines],
                              lag_ratio=0.05))

        # 左邊界嘅弧形箭嘴：向前跳（6 -> 17）、回跳（16 -> 4）
        lx = lines.get_left()[0] - 0.3

        def edge_pt(i):
            return np.array([lx, lines[i].get_center()[1], 0])

        fwd = CurvedArrow(edge_pt(6), edge_pt(13),
                          angle=TAU / 8, color=C_ERR, stroke_width=3, tip_length=0.18)
        back = CurvedArrow(edge_pt(12), edge_pt(4),
                           angle=TAU / 8, color=C_STRING, stroke_width=3, tip_length=0.18)
        self.play(Create(fwd))
        self.play(Create(back))

        math = VGroup(
            sans("跳轉目標係 pc 相對偏移：", 26, GREY_A),
            mono("target = insn_pc + offset", 24, C_ACCENT),
            mono("if_icmpge:  6 + 11  = 17", 24, C_ERR),
            mono("goto:      16 + (-12) =  4", 24, C_STRING),
            sans("偏移量係有符號 16 位 ——\n負偏移就變成迴圈。", 24, GREY_A),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.3)
        math.to_edge(RIGHT, buff=0.8).shift(UP * 0.4)
        self.play(FadeIn(math, shift=LEFT * 0.3))
        self.wait(2)

        # 用 pc 標記行一圈迴圈
        self.play(FadeOut(fwd), FadeOut(back))
        marker = Triangle(color=YELLOW, fill_opacity=1).scale(0.12).rotate(-PI / 2)

        def at(i):
            return marker.copy().next_to(lines[i], LEFT, buff=0.18)

        marker.next_to(lines[4], LEFT, buff=0.18)
        self.play(FadeIn(marker))
        for i in [5, 6, 7, 8, 9, 10, 11, 12]:
            self.play(Transform(marker, at(i)), run_time=0.28)
        # goto：跳返上去
        self.play(Transform(marker, at(4)), run_time=0.5, path_arc=PI / 2)
        again = sans("……再嚟兩次，直到 i == 3……", 22, GREY_B).next_to(lines, RIGHT, buff=0.4).shift(DOWN * 1.9)
        self.play(FadeIn(again))
        self.play(Transform(marker, at(6)), run_time=0.4)
        # 條件成立：跳去 return
        self.play(Transform(marker, at(13)), run_time=0.5, path_arc=-PI / 2)
        self.play(Flash(lines[13], color=C_STRING))
        self.narr_end()
        self.clear_scene()

    # ---------- 9：方法呼叫 ----------

    def method_calls(self):
        self.narr_start("calls")
        self.section(9, "方法呼叫：invokestatic")

        call = mono("iconst_2   iconst_3   invokestatic #1  // Test.add(II)I", 22, C_OPCODE)
        call.move_to(UP * 2.4)
        self.play(FadeIn(call))

        # 呼叫者 frame
        caller_box = RoundedRectangle(corner_radius=0.12, width=4.4, height=3.4,
                                      stroke_color=GREY_B, stroke_width=3)
        caller_box.move_to(LEFT * 3.4 + DOWN * 0.6)
        caller_cap = sans("呼叫者 frame（main）", 22, GREY_B).next_to(caller_box, UP, buff=0.15)
        cslots = VGroup(*[Square(0.85, stroke_color=C_OPCODE, stroke_width=3) for _ in range(2)])
        cslots.arrange(UP, buff=0).move_to(caller_box.get_center() + LEFT * 1.0)
        cstack_cap = sans("stack", 18, C_OPCODE).next_to(cslots, DOWN, buff=0.15)
        v2 = mono("2", 26, C_VALUE).move_to(cslots[0])
        v3 = mono("3", 26, C_VALUE).move_to(cslots[1])

        # 被呼叫者 frame
        callee_box = RoundedRectangle(corner_radius=0.12, width=4.4, height=3.4,
                                      stroke_color=C_STRING, stroke_width=3)
        callee_box.move_to(RIGHT * 3.4 + DOWN * 0.6)
        callee_cap = sans("新 frame（add）", 22, C_STRING).next_to(callee_box, UP, buff=0.15)
        lslots = VGroup(*[Square(0.85, stroke_color=C_VALUE, stroke_width=3) for _ in range(2)])
        lslots.arrange(RIGHT, buff=0).move_to(callee_box.get_center() + RIGHT * 0.0 + UP * 0.5)
        llabels = VGroup(mono("a", 18, GREY_B).next_to(lslots[0], DOWN, buff=0.1),
                         mono("b", 18, GREY_B).next_to(lslots[1], DOWN, buff=0.1))
        locals_cap = sans("locals", 18, C_VALUE).next_to(lslots, UP, buff=0.15)

        self.play(Create(caller_box), FadeIn(caller_cap), Create(cslots),
                  FadeIn(cstack_cap), FadeIn(v2), FadeIn(v3))
        self.wait(1)
        self.play(Create(callee_box), FadeIn(callee_cap), Create(lslots),
                  FadeIn(llabels), FadeIn(locals_cap))

        note = sans("參數由呼叫者嘅堆疊彈出，放入被呼叫者嘅 locals。",
                    24, GREY_A).to_edge(DOWN, buff=1.35)
        self.play(FadeIn(note))
        self.play(v3.animate.move_to(lslots[1]), run_time=0.9)
        self.play(v2.animate.move_to(lslots[0]), run_time=0.9)
        self.wait(1)

        # 遞迴
        rec = sans("interp_run() 遞迴呼叫自己 —— 一個 Java frame 對應一個 C frame。",
                   24, C_ACCENT).to_edge(DOWN, buff=0.75)
        self.play(FadeIn(rec))
        self.wait(1)

        # 結果流返去
        res = mono("5", 26, C_STRING).move_to(callee_box.get_center() + DOWN * 0.9)
        self.play(FadeIn(res, scale=1.3))
        self.play(res.animate.move_to(cslots[0]), run_time=1.0)
        back = sans("ireturn：結果推返上呼叫者嘅堆疊。",
                    24, C_STRING).to_edge(DOWN, buff=0.15)
        self.play(FadeIn(back))
        self.narr_end()
        self.clear_scene()

    # ---------- 10：println ----------

    def printing(self):
        self.narr_start("printing")
        self.section(10, "冇物件，println 點做？")

        listing = VGroup(
            mono('getstatic     #7   // System.out', 22, C_OPCODE),
            mono('ldc           #8   // "hello, world"', 22, C_OPCODE),
            mono('invokevirtual #9   // println(String)', 22, C_OPCODE),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.35)
        listing.to_edge(LEFT, buff=1.0).shift(UP * 1.2)
        self.play(FadeIn(listing))

        steps = VGroup(
            sans("getstatic System.out → 推一個假嘅 0 上去（根本冇物件！）", 24, GREY_A),
            sans("ldc → 推常數池字串嘅指標", 24, GREY_A),
            sans("invokevirtual PrintStream.println → 被攔截！", 24, C_ACCENT),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.3)
        steps.next_to(listing, DOWN, buff=0.6, aligned_edge=LEFT)

        for i, s in enumerate(steps):
            self.play(Indicate(listing[i], color=YELLOW), FadeIn(s, shift=RIGHT * 0.2))
            self.wait(0.5)

        mapping = mono('printf("%s\\n", s)', 26, C_STRING)
        term = RoundedRectangle(corner_radius=0.12, width=4.9, height=1.7,
                                stroke_color=GREY_B, fill_color=TERM_BG, fill_opacity=1)
        term.to_edge(RIGHT, buff=0.35).shift(UP * 1.6)
        term_bar = sans("終端機", 18, GREY_B).next_to(term, UP, buff=0.12)
        term_out = mono("$ ./minijvm Test.class\nhello, world", 22, C_STRING)
        term_out.move_to(term).align_to(term.get_corner(UL) + DR * 0.3, UL)

        mapping.next_to(term, DOWN, buff=0.75)
        self.play(FadeIn(term), FadeIn(term_bar), FadeIn(mapping))
        self.play(FadeIn(term_out))

        note = sans("interp.c 靠名認出 java/io/PrintStream.print/println，\nint、String 同無參數版本直接對應去 printf。",
                    24, GREY_A).to_edge(DOWN, buff=0.5)
        self.play(FadeIn(note))
        self.narr_end()
        self.clear_scene()

    # ---------- 11：指令集總覽 ----------

    def opcode_summary(self):
        self.narr_start("opcodes")
        self.section(11, "支援嘅指令集")

        groups = [
            ("常數", "iconst_m1..5  bipush  sipush  ldc"),
            ("局部變數", "iload  istore  (_0.._3)  iinc"),
            ("算術", "iadd  isub  imul  idiv  irem  ineg"),
            ("堆疊", "pop  dup  nop"),
            ("跳轉", "ifeq..ifle  if_icmpeq..le  goto"),
            ("呼叫", "invokestatic  return  ireturn"),
            ("輸出", "getstatic System.out  invokevirtual print/println"),
        ]
        rows = VGroup()
        for name, ops in groups:
            label = sans(name, 26, C_ACCENT)
            label_bg = Rectangle(width=2.9, height=0.55, stroke_width=0)
            label_grp = VGroup(label_bg, label.move_to(label_bg).align_to(label_bg, LEFT))
            code = mono(ops, 22, GREY_A)
            rows.add(VGroup(label_grp, code).arrange(RIGHT, buff=0.4, aligned_edge=DOWN))
        rows.arrange(DOWN, aligned_edge=LEFT, buff=0.34)
        rows.move_to(DOWN * 0.2)
        if rows.width > 12.6:
            rows.scale_to_fit_width(12.6)

        self.play(LaggedStart(*[FadeIn(r, shift=UP * 0.15) for r in rows], lag_ratio=0.12))
        self.wait(1.5)

        trap = sans("其他嘅？乾淨俐落咁 trap：opcode、pc、方法名 —— 然後退出。",
                    25, C_ERR).to_edge(DOWN, buff=0.5)
        trap_ex = mono('minijvm: unimplemented opcode 0xba at pc=12 in method main([Ljava/lang/String;)V',
                       17, GREY_B).next_to(trap, UP, buff=0.25)
        self.play(FadeIn(trap_ex), FadeIn(trap))
        self.narr_end()
        self.clear_scene()

    # ---------- 12：片尾 ----------

    def outro(self):
        self.narr_start("outro")
        self.section(12, "刻意留白嘅部分")

        lim = bullets([
            "冇物件、冇 heap、冇垃圾回收",
            "冇例外、冇執行緒、冇 bytecode 驗證器",
            "淨係一個 class 檔 —— 冇 classpath、冇 JDK 程式庫",
            "淨係支援 int 同 static 方法（加埋攔截咗嘅輸出）",
        ], size=27)
        lim.move_to(UP * 0.7)
        self.play(LaggedStart(*[FadeIn(r, shift=RIGHT * 0.2) for r in lim], lag_ratio=0.15))
        note = sans("剩返落嚟嘅就係精髓：解析個容器，\n再喺堆疊機上面行 fetch–decode–execute 迴圈。",
                    26, C_VALUE).next_to(lim, DOWN, buff=0.7)
        self.play(FadeIn(note))
        self.wait(3)
        self.clear_scene()

        cmds = VGroup(
            mono("$ make            # 建置 ./minijvm", 26, C_STRING),
            mono("$ make test       # javac + 行 demo", 26, C_STRING),
            mono("$ make verify     # 同真 JVM 對比輸出", 26, C_STRING),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.35)
        title = sans("自己試下", 40, weight=BOLD, color=C_OPCODE)
        grp = VGroup(title, cmds).arrange(DOWN, buff=0.7)
        self.play(FadeIn(title), FadeIn(cmds, shift=UP * 0.2))
        self.wait(3)
        self.play(FadeOut(grp))

        # SemiBlock 推介
        sb_q = sans("中小學生想自己玩下 bytecode？", 34, C_VALUE)
        sb_name = sans("SemiBlock", 72, weight=BOLD, color=C_STRING)
        sb_box = SurroundingRectangle(sb_name, color=C_STRING, buff=0.45, corner_radius=0.18)
        sb_sub = sans("香港編程學會自主研發 · 內置 JVM 模擬器", 28, GREY_A)
        sb = VGroup(sb_q, VGroup(sb_name, sb_box), sb_sub).arrange(DOWN, buff=0.65)
        self.play(FadeIn(sb_q, shift=UP * 0.2))
        self.play(Write(sb_name), Create(sb_box))
        self.play(FadeIn(sb_sub, shift=UP * 0.2))

        end = sans("Hong Kong Programming Society · Peter", 28, GREY_A)
        end.to_edge(DOWN, buff=0.55)
        self.play(FadeIn(end))
        self.narr_end()
        self.play(*[FadeOut(m) for m in self.mobjects], run_time=1.2)
        self.wait(0.5)
