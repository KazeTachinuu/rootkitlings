const textColor = { blue: 'text-blue', green: 'text-green', purple: 'text-purple', amber: 'text-amber' }
const bgColor = { blue: 'bg-blue', green: 'bg-green', purple: 'bg-purple', amber: 'bg-amber' }
const iconFg = { blue: 'text-white', green: 'text-white', purple: 'text-white', amber: 'text-[#1a1028]' }

export function StatCard({ label, value, color, icon: Icon }) {
  return (
    <div className="nb-card p-4 flex items-center justify-between gap-4">
      <div>
        <div className="text-[10px] font-bold uppercase tracking-widest text-fg-3">{label}</div>
        <div className={`text-3xl font-bold font-mono ${textColor[color] || ''}`}>{value}</div>
      </div>
      <div className={`w-10 h-10 rounded-[var(--radius)] flex items-center justify-center ${bgColor[color] || ''} border-2 border-border shadow-[2px_2px_0px_0px_var(--color-border)]`}>
        <Icon size={20} className={iconFg[color] || 'text-white'} />
      </div>
    </div>
  )
}
