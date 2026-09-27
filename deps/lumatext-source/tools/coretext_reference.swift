import Foundation
import CoreGraphics
import CoreText
import CryptoKit
import ImageIO
import UniformTypeIdentifiers

struct GlyphRecord: Codable {
    let glyph: UInt16
    let cluster: Int
    let x: Double
    let y: Double
    let advance: Double
    let font: String
}

struct RenderRecord: Codable {
    let name: String
    let text: String
    let weight: Int
    let size: Double
    let scale: Double
    let background: String
    let image: String
    let mask: String
    let width: Double
    let ascent: Double
    let descent: Double
    let leading: Double
    let glyphs: [GlyphRecord]
    let fallbackCase: Bool
}

struct Manifest: Codable {
    let renderer: String
    let osBuild: String
    let regularSHA256: String
    let boldSHA256: String
    let foregroundLight: [Double]
    let foregroundDark: [Double]
    let backgroundLight: [Double]
    let backgroundDark: [Double]
    let records: [RenderRecord]
}

struct CorpusCase {
    let name: String
    let text: String
    let size: CGFloat
    let weight: Int
    let fallback: Bool
}

func argument(_ name: String) -> String {
    guard let index = CommandLine.arguments.firstIndex(of: name),
          index + 1 < CommandLine.arguments.count else {
        fputs("missing argument \(name)\n", stderr)
        exit(2)
    }
    return CommandLine.arguments[index + 1]
}

func sha256(_ url: URL) throws -> String {
    let data = try Data(contentsOf: url)
    return SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
}

func loadFont(_ url: URL, size: CGFloat) throws -> CTFont {
    let data = try Data(contentsOf: url) as CFData
    guard let provider = CGDataProvider(data: data), let cgFont = CGFont(provider) else {
        throw NSError(domain: "LumaTextCoreText", code: 1,
                      userInfo: [NSLocalizedDescriptionKey: "cannot load font \(url.path)"])
    }
    return CTFontCreateWithGraphicsFont(cgFont, size, nil, nil)
}

func writePNG(_ image: CGImage, to url: URL) throws {
    guard let destination = CGImageDestinationCreateWithURL(
        url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
        throw NSError(domain: "LumaTextCoreText", code: 2)
    }
    CGImageDestinationAddImage(destination, image, nil)
    if !CGImageDestinationFinalize(destination) {
        throw NSError(domain: "LumaTextCoreText", code: 3)
    }
}

let regularURL = URL(fileURLWithPath: argument("--regular"))
let boldURL = URL(fileURLWithPath: argument("--bold"))
let outputURL = URL(fileURLWithPath: argument("--output"), isDirectory: true)
try FileManager.default.createDirectory(at: outputURL,
                                        withIntermediateDirectories: true)

let corpus: [CorpusCase] = [
    .init(name: "quick_12_regular", text: "Quick Look", size: 12, weight: 400, fallback: false),
    .init(name: "quick_16_bold", text: "Quick Look", size: 16, weight: 700, fallback: false),
    .init(name: "latin_13_regular", text: "Mixed filename README.md 0123456789", size: 13, weight: 400, fallback: false),
    .init(name: "cjk_14_regular", text: "项目计划 2026-08-24.txt 中文标点，引号", size: 14, weight: 400, fallback: false),
    .init(name: "cjk_16_bold", text: "粗体观感 Quick Look 文件列表", size: 16, weight: 700, fallback: false),
    .init(name: "arabic_14_fallback", text: "ملف Quick 123", size: 14, weight: 400, fallback: true),
    .init(name: "hebrew_14_fallback", text: "קובץ Quick 123", size: 14, weight: 400, fallback: true)
]
let scales: [CGFloat] = [1.0, 1.25, 1.5, 2.0, 3.0]
guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
      let lightBackground = CGColor(colorSpace: colorSpace,
          components: [0.965, 0.969, 0.973, 1]),
      let darkBackground = CGColor(colorSpace: colorSpace,
          components: [0.09, 0.098, 0.11, 1]),
      let lightForeground = CGColor(colorSpace: colorSpace,
          components: [0.09, 0.098, 0.11, 1]),
      let darkForeground = CGColor(colorSpace: colorSpace,
          components: [0.91, 0.918, 0.929, 1]),
      let maskForeground = CGColor(colorSpace: colorSpace,
          components: [1, 1, 1, 1]) else {
    throw NSError(domain: "LumaTextCoreText", code: 4,
                  userInfo: [NSLocalizedDescriptionKey: "cannot create sRGB colors"])
}
var records: [RenderRecord] = []

for item in corpus {
    let fontURL = item.weight >= 600 ? boldURL : regularURL
    let font = try loadFont(fontURL, size: item.size)
    for scale in scales {
        for backgroundName in ["light", "dark"] {
            let foreground = backgroundName == "light" ? lightForeground : darkForeground
            let background = backgroundName == "light" ? lightBackground : darkBackground
            let attributed = NSAttributedString(string: item.text, attributes: [
                NSAttributedString.Key(kCTFontAttributeName as String): font,
                NSAttributedString.Key(kCTForegroundColorAttributeName as String): foreground
            ])
            let line = CTLineCreateWithAttributedString(attributed)
            var ascent: CGFloat = 0
            var descent: CGFloat = 0
            var leading: CGFloat = 0
            let lineWidth = CGFloat(CTLineGetTypographicBounds(
                line, &ascent, &descent, &leading))
            let widthDIP: CGFloat = 800
            let heightDIP: CGFloat = 80
            let pixelWidth = Int(ceil(widthDIP * scale))
            let pixelHeight = Int(ceil(heightDIP * scale))
            guard let context = CGContext(data: nil, width: pixelWidth, height: pixelHeight,
                bitsPerComponent: 8, bytesPerRow: pixelWidth * 4, space: colorSpace,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
                throw NSError(domain: "LumaTextCoreText", code: 4)
            }
            context.scaleBy(x: scale, y: scale)
            context.setFillColor(background)
            context.fill(CGRect(x: 0, y: 0, width: widthDIP, height: heightDIP))
            context.textMatrix = .identity
            context.textPosition = CGPoint(x: 8, y: heightDIP - 8 - ascent)
            CTLineDraw(line, context)
            guard let image = context.makeImage() else {
                throw NSError(domain: "LumaTextCoreText", code: 5)
            }
            let scaleName = String(format: "%.2f", Double(scale)).replacingOccurrences(of: ".", with: "_")
            let imageName = "\(item.name)__\(backgroundName)__\(scaleName).png"
            try writePNG(image, to: outputURL.appendingPathComponent(imageName))

            let maskAttributed = NSAttributedString(string: item.text, attributes: [
                NSAttributedString.Key(kCTFontAttributeName as String): font,
                NSAttributedString.Key(kCTForegroundColorAttributeName as String): maskForeground
            ])
            let maskLine = CTLineCreateWithAttributedString(maskAttributed)
            guard let maskContext = CGContext(data: nil, width: pixelWidth, height: pixelHeight,
                bitsPerComponent: 8, bytesPerRow: pixelWidth * 4, space: colorSpace,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
                throw NSError(domain: "LumaTextCoreText", code: 6)
            }
            maskContext.scaleBy(x: scale, y: scale)
            maskContext.clear(CGRect(x: 0, y: 0, width: widthDIP, height: heightDIP))
            maskContext.textMatrix = .identity
            maskContext.textPosition = CGPoint(x: 8, y: heightDIP - 8 - ascent)
            CTLineDraw(maskLine, maskContext)
            guard let maskImage = maskContext.makeImage() else {
                throw NSError(domain: "LumaTextCoreText", code: 7)
            }
            let maskName = "\(item.name)__\(backgroundName)__\(scaleName).mask.png"
            try writePNG(maskImage, to: outputURL.appendingPathComponent(maskName))

            var glyphRecords: [GlyphRecord] = []
            let runs = CTLineGetGlyphRuns(line) as! [CTRun]
            for run in runs {
                let count = CTRunGetGlyphCount(run)
                var glyphs = Array(repeating: CGGlyph(), count: count)
                var positions = Array(repeating: CGPoint.zero, count: count)
                var advances = Array(repeating: CGSize.zero, count: count)
                var indices = Array(repeating: CFIndex(), count: count)
                CTRunGetGlyphs(run, CFRange(location: 0, length: 0), &glyphs)
                CTRunGetPositions(run, CFRange(location: 0, length: 0), &positions)
                CTRunGetAdvances(run, CFRange(location: 0, length: 0), &advances)
                CTRunGetStringIndices(run, CFRange(location: 0, length: 0), &indices)
                let attributes = CTRunGetAttributes(run) as NSDictionary
                let runFont = attributes[kCTFontAttributeName] as! CTFont
                let fontName = CTFontCopyPostScriptName(runFont) as String
                for index in 0..<count {
                    glyphRecords.append(.init(glyph: glyphs[index], cluster: indices[index],
                        x: Double(positions[index].x), y: Double(positions[index].y),
                        advance: Double(advances[index].width), font: fontName))
                }
            }
            records.append(.init(name: item.name, text: item.text, weight: item.weight,
                size: Double(item.size), scale: Double(scale), background: backgroundName,
                image: imageName, mask: maskName, width: Double(lineWidth), ascent: Double(ascent),
                descent: Double(descent), leading: Double(leading), glyphs: glyphRecords,
                fallbackCase: item.fallback))
        }
    }
}

let manifest = Manifest(renderer: "CoreText/CoreGraphics",
    osBuild: ProcessInfo.processInfo.operatingSystemVersionString,
    regularSHA256: try sha256(regularURL), boldSHA256: try sha256(boldURL),
    foregroundLight: [0.09, 0.098, 0.11, 1],
    foregroundDark: [0.91, 0.918, 0.929, 1],
    backgroundLight: [0.965, 0.969, 0.973, 1],
    backgroundDark: [0.09, 0.098, 0.11, 1], records: records)
let encoder = JSONEncoder()
encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
try encoder.encode(manifest).write(to: outputURL.appendingPathComponent("manifest.json"))
