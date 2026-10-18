import SwiftUI

struct AlarmsView: View {
    @Environment(AppModel.self) private var model
    @State private var editing: Alarm?
    @State private var creating = false

    var body: some View {
        NavigationStack {
            List {
                if model.state?.state == "ringing" {
                    Section {
                        Button {
                            Task { await model.stopAlarm() }
                        } label: {
                            Label("Vypnout zvonící budík", systemImage: "alarm.waves.left.and.right")
                                .font(.headline)
                        }
                    }
                }
                if let next = model.state?.nextAlarm {
                    Section {
                        VStack(alignment: .leading, spacing: 4) {
                            Caption(text: "Další budík")
                            Text(next.date, format: .dateTime.weekday(.wide).hour().minute())
                                .font(.title2.weight(.medium))
                            if !next.label.isEmpty {
                                Text(next.label)
                                    .foregroundStyle(Theme.muted)
                            }
                        }
                        .padding(.vertical, 4)
                    }
                }
                Section("Budíky") {
                    if model.alarms.isEmpty {
                        Text("Žádný budík. Řekni „Jarvisi, vzbuď mě v sedm“ nebo přidej budík tady.")
                            .font(.callout)
                            .foregroundStyle(Theme.muted)
                    }
                    ForEach(model.alarms) { alarm in
                        AlarmRow(alarm: alarm)
                            .contentShape(Rectangle())
                            .onTapGesture { editing = alarm }
                            .swipeActions {
                                Button(role: .destructive) {
                                    Task { await model.delete(alarm) }
                                } label: {
                                    Label("Smazat", systemImage: "trash")
                                }
                            }
                    }
                }
                if !model.reminders.isEmpty {
                    Section("Připomínky od Jarvise") {
                        ForEach(model.reminders) { reminder in
                            VStack(alignment: .leading, spacing: 3) {
                                Text(reminder.text)
                                Text(reminder.date, format: .dateTime.weekday(.abbreviated).day().month().hour().minute())
                                    .font(.footnote)
                                    .foregroundStyle(Theme.muted)
                            }
                            .swipeActions {
                                Button(role: .destructive) {
                                    Task { await model.delete(reminder) }
                                } label: {
                                    Label("Zrušit", systemImage: "trash")
                                }
                            }
                        }
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(Theme.background)
            .navigationTitle("Budíky")
            .toolbar {
                Button {
                    creating = true
                } label: {
                    Image(systemName: "plus")
                }
            }
            .safeAreaInset(edge: .top) { OfflineBanner() }
            .refreshable { await model.refresh() }
            .sheet(item: $editing) { alarm in
                AlarmEditor(alarm: alarm)
            }
            .sheet(isPresented: $creating) {
                AlarmEditor(alarm: nil)
            }
        }
    }
}

private struct AlarmRow: View {
    @Environment(AppModel.self) private var model
    let alarm: Alarm

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(alarm.timeText)
                    .font(.system(size: 40, weight: .light))
                    .monospacedDigit()
                    .foregroundStyle(alarm.enabled ? .white : Theme.muted)
                Text([alarm.label, alarm.repeatText].filter { !$0.isEmpty }.joined(separator: " · "))
                    .font(.footnote)
                    .foregroundStyle(Theme.muted)
            }
            Spacer()
            Toggle("", isOn: Binding(get: { alarm.enabled }, set: { value in
                Task { await model.setEnabled(alarm, value) }
            }))
            .labelsHidden()
        }
    }
}

struct AlarmEditor: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    let alarm: Alarm?
    @State private var time = Date()
    @State private var days = 0
    @State private var label = ""

    var body: some View {
        NavigationStack {
            Form {
                DatePicker("Čas", selection: $time, displayedComponents: .hourAndMinute)
                    .datePickerStyle(.wheel)
                    .labelsHidden()
                    .frame(maxWidth: .infinity)
                Section("Opakovat") {
                    HStack(spacing: 6) {
                        ForEach(Weekday.all) { day in
                            let on = days & day.bit != 0
                            Button(day.short) {
                                days ^= day.bit
                            }
                            .buttonStyle(.plain)
                            .font(.callout.weight(.medium))
                            .frame(maxWidth: .infinity, minHeight: 36)
                            .background(on ? Theme.accent : Theme.edge, in: Circle())
                            .foregroundStyle(on ? Color.black : .white)
                        }
                    }
                    Picker("Rychle", selection: $days) {
                        Text("Jednou").tag(0)
                        Text("Všední dny").tag(Alarm.weekdays)
                        Text("Každý den").tag(Alarm.everyDay)
                    }
                    .pickerStyle(.segmented)
                }
                Section("Popis") {
                    TextField("Třeba Do práce", text: $label)
                }
                if let alarm {
                    Section {
                        Button("Smazat budík", role: .destructive) {
                            Task {
                                await model.delete(alarm)
                                dismiss()
                            }
                        }
                    }
                }
            }
            .navigationTitle(alarm == nil ? "Nový budík" : "Budík")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Zrušit") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Uložit") {
                        Task {
                            await save()
                            dismiss()
                        }
                    }
                }
            }
        }
        .onAppear(perform: load)
    }

    private func load() {
        guard let alarm else {
            return
        }
        days = alarm.days
        label = alarm.label
        time = Calendar.current.date(bySettingHour: alarm.hour, minute: alarm.minute, second: 0, of: Date()) ?? Date()
    }

    private func save() async {
        let parts = Calendar.current.dateComponents([.hour, .minute], from: time)
        let draft = AlarmDraft(hour: parts.hour ?? 7, minute: parts.minute ?? 0, days: days,
                               label: label.trimmingCharacters(in: .whitespaces), enabled: true)
        await model.save(draft, id: alarm?.id)
    }
}
