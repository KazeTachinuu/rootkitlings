const colors = { connect: 'bg-green text-white', disconnect: 'bg-red text-white', cmd: 'bg-blue text-white', sys: 'bg-bg-2' }

export function EventBadge({ type }) {
  return <span className={`nb-badge ${colors[type] || 'bg-bg-2'} w-24 justify-center`}>{type}</span>
}
