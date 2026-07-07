# minijvm 講解影片（廣東話版）

用 [Manim Community](https://www.manim.community/) 整嘅 minijvm 完整動畫講解，
配 macOS「善怡」(Sinji, zh_HK) 廣東話旁白。片頭係
「Hong Kong Programming Society - Peter」，之後分 12 節：

1. 成個流程 —— `.java` → `javac` → `.class` → minijvm
2. `.class` 檔案結構（magic、常數池、方法表）
3. 常數池同索引解析
4. `Code` 屬性 —— bytecode 住喺邊
5. 啟動：`main.c` 做咩
6. 解譯器 frame 同 fetch–decode–execute 迴圈
7. 一步一步執行 `add(2, 3)`
8. 跳轉同迴圈（pc 相對偏移、負偏移回跳）
9. `invokestatic` —— 一個 Java frame 對應一個 C frame
10. 冇物件之下 `System.out.println` 點做
11. 支援嘅指令集同 trap 行為
12. 刻意留白嘅部分，同埋點樣自己試

## 準備同渲染

需要 Python 3 同 ffmpeg（`brew install ffmpeg`）；旁白用 macOS 內置
`say` 指令生成，需要「善怡 (Sinji)」語音。

```sh
cd video
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
python3 gen_narration.py                    # 生成 narration/*.aiff 旁白
.venv/bin/manim render -qm minijvm_explainer.py MiniJVMExplainer
```

成品喺 `media/videos/minijvm_explainer/720p30/MiniJVMExplainer.mp4`。
想要 1080p60 用 `-qh`，快速預覽用 `-ql`。
