"""Generate Cantonese narration audio with AWS Polly (voice: Hiujin, yue-CN).

Credentials are read from the standard AWS locations (environment variables
AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY, or ~/.aws/credentials).

Run:  .venv/bin/python gen_narration.py
Writes one .mp3 per section into narration/.
"""

import io
from pathlib import Path

import boto3
from pydub import AudioSegment

TARGET_PEAK_DBFS = -3.0  # Polly 輸出偏細聲，統一做 peak normalize

NARRATION = {
    "intro": (
        "大家好，我係Peter，歡迎嚟到香港編程學會。"
        "今日同大家拆解一個用純C語言寫嘅迷你JVM，"
        "由一個class檔嘅原始位元組，一路講到bytecode點樣真係行起上嚟。"
    ),
    "pipeline": (
        "首先睇下成個流程。javac將Java原始碼編譯做class檔，"
        "入面裝住俾堆疊機執行嘅bytecode，然後mini J V M負責解析同執行。"
        "mini J V M得三個檔案：main點c係入口，classfile點c負責解析，"
        "interp點c就係個解譯器。"
    ),
    "classfile": (
        "class檔其實係一串big endian嘅位元組。"
        "classfile點c用一個好簡單嘅游標，由頭到尾讀一次。"
        "頭四個位元組一定要係CAFE BABE，唔係嘅話就唔係class檔。"
        "最重要嘅係常數池同埋方法表，其他用唔到嘅部分就直接跳過。"
    ),
    "constpool": (
        "常數池係一個有編號嘅表，所有名稱、字串、數字都放晒喺度，"
        "其他嘢全部靠索引指返入嚟。"
        "想知一號嘅Methodref係咩，就要跟住啲索引一路追落去，"
        "最後就砌返出Test點add呢個完整嘅方法簽名。"
    ),
    "codeattr": (
        "每個方法都帶住自己嘅Code屬性，"
        "入面有堆疊上限、局部變數數目，同埋真正嘅bytecode。"
        "好似add呢個方法，成個方法體其實得四個位元組，"
        "每一個位元組就係一個opcode。"
    ),
    "startup": (
        "main點c做嘅嘢好簡單：載入class檔，搵到main方法，"
        "然後交俾interp run開始執行。成個driver得廿八行，"
        "所有重頭戲都喺解譯器度。"
    ),
    "frame": (
        "解譯器入面，每個方法有自己一個frame，"
        "包括局部變數、操作數堆疊、堆疊指標同程式計數器。"
        "JVM係一部堆疊機，冇暫存器，所有指令都係圍住個堆疊推入彈出。"
        "個主迴圈就係經典嘅fetch、decode、execute三部曲。"
    ),
    "addwalk": (
        "而家實際行一次add兩三。參數一開始擺喺局部變數度。"
        "iload零將2推上堆疊，iload一推埋3上去。"
        "iadd彈出兩個數，加埋得5，再推返上堆疊。"
        "最後ireturn彈出堆疊頂，呢個就係回傳值。"
    ),
    "loop": (
        "跳轉指令用相對偏移：目標位置等於指令位置加偏移量。"
        "偏移量係有符號十六位，負數就可以跳返轉頭，變成迴圈。"
        "睇下個黃色標記行一圈：去到goto就跳返上去，"
        "直到i等於三，條件成立就跳出去return。"
    ),
    "calls": (
        "invokestatic會將參數由呼叫者嘅堆疊彈出，"
        "放入新frame嘅局部變數。interp run會遞迴咁呼叫自己，"
        "一個Java frame就對應一個C frame。"
        "方法return之後，結果會推返上呼叫者嘅堆疊。"
    ),
    "printing": (
        "mini J V M冇物件，咁println點算呢？"
        "getstatic System out會推一個假嘅零上去，"
        "ldc推個字串指標，然後invokevirtual println會被攔截，"
        "直接轉做C語言嘅printf。"
    ),
    "opcodes": (
        "支援嘅指令就係咁多：常數、局部變數、算術、堆疊操作、"
        "跳轉、方法呼叫，同埋攔截咗嘅輸出。"
        "遇到唔識嘅opcode，就會清楚咁報錯，"
        "話你知邊個opcode、喺邊個位置。"
    ),
    "outro": (
        "mini J V M刻意冇物件、冇垃圾回收、冇例外、冇執行緒。"
        "剩返落嚟嘅就係JVM嘅精髓：解析個容器，"
        "然後行一個堆疊機嘅fetch decode execute主迴圈。"
        "想自己試嘅話，make一下就得。"
        "另外，中小學生如果想自己玩下bytecode，"
        "可以去我哋學會自主研發嘅Semi Block度試下，"
        "入面有JVM模擬器。多謝收睇，我哋下次見！"
    ),
}


def main():
    polly = boto3.client("polly", region_name="us-east-1")
    out = Path(__file__).parent / "narration"
    out.mkdir(exist_ok=True)
    for name, text in NARRATION.items():
        resp = polly.synthesize_speech(
            Text=text,
            VoiceId="Hiujin",
            Engine="neural",
            LanguageCode="yue-CN",
            OutputFormat="mp3",
            SampleRate="24000",
        )
        seg = AudioSegment.from_file(io.BytesIO(resp["AudioStream"].read()), format="mp3")
        seg = seg.apply_gain(TARGET_PEAK_DBFS - seg.max_dBFS)
        path = out / f"{name}.mp3"
        seg.export(str(path), format="mp3", bitrate="128k")
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
