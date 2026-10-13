const styles = {
  green: 'bg-green text-white',
  red: 'bg-red text-white',
  amber: 'bg-amber text-[#1a1028]',
  blue: 'bg-blue text-white',
  purple: 'bg-purple text-white',
  main: 'bg-main text-main-fg',
}

export function Badge({ label, color = 'main' }) {
  return (
    <span className={`nb-badge ${styles[color] || styles.main}`}>
      <span className={`w-1.5 h-1.5 rounded-full bg-current ${color === 'green' ? 'pulse' : ''}`} />
      {label}
    </span>
  )
}

export function StatusPill({ ok }) {
  if (ok === null) return <span className={`nb-badge ${styles.amber}`}>PENDING</span>
  return ok
    ? <span className={`nb-badge ${styles.green}`}>OK</span>
    : <span className={`nb-badge ${styles.red}`}>ERR</span>
}
