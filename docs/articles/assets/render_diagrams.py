"""Render the article's semantic GIFs and static posters (requires Pillow).

Usage: python render_diagrams.py --font PATH_TO_CHINESE_FONT
These diagrams illustrate protocols; they do not capture a real ART device.
"""
from argparse import ArgumentParser
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

BG = '#fffefa'
INK = '#172c30'
MUTED = '#526568'
LINE = '#d6dfda'
TEAL = '#087e80'
MINT = '#d4eee5'
SAND = '#fff0d4'
AMBER = '#9b5712'
W, H = 760, 470


def render(font_path, output):
    font = lambda size: ImageFont.truetype(str(font_path), size=size)
    title, text, small = font(32), font(27), font(23)

    def base(label, phase):
        im = Image.new('RGB', (W, H), BG)
        d = ImageDraw.Draw(im)
        d.text((30, 20), label, font=title, fill=INK)
        d.text((30, 67), phase, font=text, fill=TEAL)
        d.line((30, 112, 730, 112), fill=LINE, width=2)
        return im, d

    def box(d, xy, label, lines, active=False, warning=False):
        fill = SAND if warning else MINT if active else '#f5f4ef'
        d.rounded_rectangle(xy, radius=16, fill=fill, outline=LINE, width=1)
        x, y = xy[0] + 20, xy[1] + 14
        d.text((x, y), label, font=text, fill=INK)
        for i, line in enumerate(lines):
            d.text((x, y + 49 + i * 36), line, font=small, fill=MUTED)

    def arrow(d, y, progress, warning=False):
        color = AMBER if warning else TEAL
        d.line((340, y, 420, y), fill=LINE, width=3)
        d.polygon([(420, y), (410, y - 7), (410, y + 7)], fill=LINE)
        x = 344 + progress * 65
        d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=color)

    def consistency(stage, progress=1):
        phases = ['① 旧定义运行，新定义待提交', '② 先准备映射与强根',
                  '③ 安全窗口中协调更新', '④ 完成提交，新定义生效']
        im, d = base('结构重定义：状态与访问一起更新', phases[stage])
        committed = stage == 3
        box(d, (30, 140, 330, 323), '已有 Counter 对象',
            ['count = 7 · 始终保留', 'label = null · 新字段'] if committed else
            ['count = 7', '对象状态仍然有效'], active=committed)
        box(d, (430, 140, 730, 323), '方法与访问路径',
            ['value() → 14', '缓存与引用已协调'] if committed else
            ['V1：value() → 7', 'V2：候选定义'], active=committed)
        arrow(d, 230, progress)
        messages = ['当前访问按 V1 语义执行', '可失败的工作尽量在提交前完成',
                    '阶段示意：不向调用方暴露半成品', '后续调用使用 V2，状态保持连续']
        d.text((30, 352), messages[stage], font=text, fill=INK)
        d.text((30, 398), '语义示意 · 不表示物理地址或内部执行时长', font=small, fill=MUTED)
        return im

    def retry(stage, progress=1):
        phases = ['① 活动 AOT 帧缺少恢复条件', '② 本次变更在提交前拒绝',
                  '③ 旧调用完成，活动帧退出', '④ 重新验证后重试提交']
        im, d = base('活动帧：拒绝、退出、再重试', phases[stage])
        box(d, (30, 140, 330, 323), '正在执行的旧调用',
            ['旧帧继续使用 V1', '恢复信息不足'] if stage < 2 else
            ['旧调用已经完成', '目标活动帧已退出'], warning=stage < 2, active=stage >= 2)
        box(d, (430, 140, 730, 323), '重定义事务',
            ['V2 · 候选批次', 'V1 · 当前有效定义'] if stage == 0 else
            ['V2 · 本次未提交', 'V1 · 完整保留'] if stage == 1 else
            ['重新核对所有前提', '准备新的重定义请求'] if stage == 2 else
            ['校验满足才提交', '后续调用使用 V2'], warning=stage == 1, active=stage == 3)
        arrow(d, 230, progress, warning=stage == 1)
        messages = ['不能凭“暂停了”推断机器码能恢复', '一次拒绝不会让旧定义部分生效',
                    '旧帧退出只是重试条件之一', '重试是新事务，仍需完整校验']
        d.text((30, 352), messages[stage], font=text, fill=INK)
        d.text((30, 398), '非调试 AOT 场景示意 · 没有即时去优化', font=small, fill=MUTED)
        return im

    def save(name, draw):
        frames, durations = [], []
        for stage in range(4):
            for i in range(12):
                frames.append(draw(stage, i / 11))
                durations.append(80 if i < 11 else 1300)
        # A shared palette keeps colors stable; omitting loop plays once.
        sample = Image.new('RGB', (W * 2, H * 2))
        for stage in range(4):
            sample.paste(draw(stage), ((stage % 2) * W, (stage // 2) * H))
        palette = sample.quantize(colors=128)
        indexed = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in frames]
        indexed[0].save(output / (name + '.gif'), save_all=True,
                        append_images=indexed[1:], duration=durations,
                        disposal=1, optimize=True)
        draw(3).save(output / (name + '-poster.png'), optimize=True)

    output.mkdir(parents=True, exist_ok=True)
    save('consistent-publication', consistency)
    save('active-frame-retry', retry)


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--font', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    render(args.font, args.output)
