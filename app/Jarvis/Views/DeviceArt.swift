import SwiftUI

struct DeviceArt: View {
    var time = Date()
    var animated = true

    var body: some View {
        TimelineView(.animation(paused: !animated)) { timeline in
            let turn = timeline.date.timeIntervalSinceReferenceDate
            GeometryReader { geometry in
                let size = min(geometry.size.width, geometry.size.height)
                ZStack {
                    Image("DevicePhoto")
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .clipShape(RoundedRectangle(cornerRadius: size * 0.012, style: .continuous))
                        .shadow(color: Theme.accent.opacity(0.18), radius: size * 0.08, y: size * 0.02)
                    screen(size: size * 0.842, turn: turn)
                }
                .frame(width: size, height: size)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .aspectRatio(1, contentMode: .fit)
        .accessibilityLabel("Displej Waveshare ESP32-S3 Touch LCD 4B s Jarvisem")
    }

    private func screen(size: CGFloat, turn: Double) -> some View {
        ZStack {
            Color.black
            ForEach(0..<4) { ring in
                let radius = size * (0.44 - Double(ring) * 0.075)
                Circle()
                    .trim(from: 0.04 + Double(ring) * 0.05, to: 0.84 - Double(ring) * 0.06)
                    .stroke(AngularGradient(colors: [Theme.accent.opacity(0.1), Theme.accent, Color.white.opacity(0.9)],
                                            center: .center),
                            style: StrokeStyle(lineWidth: size * (0.012 - Double(ring) * 0.002), lineCap: .round))
                    .frame(width: radius * 2, height: radius * 2)
                    .rotationEffect(.degrees(turn * (ring.isMultiple(of: 2) ? 9 : -6) + Double(ring) * 40))
            }
            Circle()
                .fill(RadialGradient(colors: [Color.white, Theme.accent, Color.purple.opacity(0.6), .clear], center: .center,
                                     startRadius: 0, endRadius: size * 0.09))
                .frame(width: size * 0.18, height: size * 0.18)
                .blur(radius: size * 0.004)
                .scaleEffect(1 + 0.05 * sin(turn * 1.7))
                .offset(y: size * 0.24)
            VStack(spacing: size * 0.01) {
                Text(time, format: .dateTime.hour().minute())
                    .font(.system(size: size * 0.17, weight: .thin))
                    .monospacedDigit()
                Text("J.A.R.V.I.S.")
                    .font(.system(size: size * 0.035, weight: .medium))
                    .tracking(size * 0.012)
                    .foregroundStyle(Theme.muted)
            }
            .foregroundStyle(.white)
            .offset(y: -size * 0.04)
        }
        .frame(width: size, height: size)
    }
}
