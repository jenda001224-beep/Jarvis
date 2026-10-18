import AppKit

let size = 1024.0
let image = NSImage(size: NSSize(width: size, height: size))
image.lockFocus()
let context = NSGraphicsContext.current!.cgContext
context.setFillColor(CGColor(red: 0.02, green: 0.03, blue: 0.05, alpha: 1))
context.fill(CGRect(x: 0, y: 0, width: size, height: size))
let center = CGPoint(x: size / 2, y: size / 2)
let accent = CGColor(red: 0.36, green: 0.84, blue: 1.0, alpha: 1)
let rings: [(radius: Double, width: Double, start: Double, end: Double, alpha: Double)] = [
    (390, 20, 20, 300, 1.0), (330, 12, 140, 400, 0.75), (275, 8, 250, 470, 0.55),
]
for ring in rings {
    context.setStrokeColor(accent.copy(alpha: ring.alpha)!)
    context.setLineWidth(ring.width)
    context.setLineCap(.round)
    context.addArc(center: center, radius: ring.radius, startAngle: ring.start * .pi / 180, endAngle: ring.end * .pi / 180,
                   clockwise: false)
    context.strokePath()
}
let colors = [CGColor(red: 1, green: 1, blue: 1, alpha: 1), accent, CGColor(red: 0.45, green: 0.25, blue: 0.95, alpha: 0.7),
              CGColor(red: 0.02, green: 0.03, blue: 0.05, alpha: 0)] as CFArray
let gradient = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors, locations: [0, 0.35, 0.7, 1])!
context.drawRadialGradient(gradient, startCenter: center, startRadius: 0, endCenter: center, endRadius: 200, options: [])
image.unlockFocus()
let bitmap = NSBitmapImageRep(data: image.tiffRepresentation!)!
let png = bitmap.representation(using: .png, properties: [:])!
try! png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
