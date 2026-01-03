const REPO_OWNER = 'recaner35';
const REPO_NAME = 'HorusByWyntro';

document.addEventListener('DOMContentLoaded', () => {
    fetchVersion();
    fetchChangelog();
});

// 1. Fetch current version from version.json (hosted on Pages)
async function fetchVersion() {
    try {
        // Since version.json is in the root of the site
        const response = await fetch('version.json');
        if (!response.ok) throw new Error('Version file not found');

        const data = await response.json();

        // Update UI
        document.getElementById('currentVersion').textContent = `v${data.version}`;

        const btn = document.getElementById('downloadBtn');
        btn.href = data.url;
        // Make button active style if needed, it's already styled

    } catch (error) {
        console.error('Error fetching version:', error);
        document.getElementById('currentVersion').textContent = 'Unknown';
    }
}

// 2. Fetch Release History from GitHub API
async function fetchChangelog() {
    const listContainer = document.getElementById('changelogList');

    try {
        const response = await fetch(`https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}/releases`);
        if (!response.ok) throw new Error('GitHub API Error');

        const releases = await response.json();

        listContainer.innerHTML = ''; // Clear loading

        if (releases.length === 0) {
            listContainer.innerHTML = '<p class="text-center">No releases found.</p>';
            return;
        }

        releases.forEach(release => {
            const date = new Date(release.published_at).toLocaleDateString(undefined, {
                year: 'numeric', month: 'long', day: 'numeric'
            });

            // Convert Markdown-like body to HTML (Simple conversion)
            // For full markdown support, a library like marked.js would be needed.
            // Here we just handle newlines and bullets simply.
            let bodyHtml = release.body
                .replace(/\r\n/g, '<br>')
                .replace(/\n/g, '<br>');

            const item = document.createElement('div');
            item.className = 'release-item';
            item.innerHTML = `
                <div class="release-header">
                    <span class="release-tag">${release.tag_name}</span>
                    <span class="release-date">${date}</span>
                </div>
                <div class="release-body">
                    ${bodyHtml}
                </div>
            `;

            listContainer.appendChild(item);
        });

    } catch (error) {
        console.error('Error fetching changelog:', error);
        listContainer.innerHTML = '<p style="color:red; text-align:center;">Failed to load release history.</p>';
    }
}
