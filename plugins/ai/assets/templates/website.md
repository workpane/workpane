# Website engineering

## Recognize the project

- Identify the stack from its files: `astro.config.*` for Astro, `eleventy.config.*` or `.eleventy.js` for Eleventy, `hugo.toml`, `hugo.yaml` or `config.toml` with `content/` and `layouts/` for Hugo, `_config.yml` with a `Gemfile` for Jekyll, and plain `.html` files with no generator for a hand-written static site. A framework such as Next.js or SvelteKit used for a marketing site follows its own platform rules plus these.
- Read the manifest (`package.json`, `Gemfile`, the Hugo version pinned in the continuous integration or `netlify.toml`) and the scripts that build, serve, lint and check the site. Detect the package manager from the lock file, and never create a second one.
- Read the deployment configuration to learn the host and its rules: `netlify.toml`, `vercel.json`, `_headers` and `_redirects` (Netlify and Cloudflare Pages), `wrangler.toml`, the workflow that publishes to GitHub Pages, or the web server configuration.
- Find where content lives (Markdown collections, data files, a headless CMS) and where layout lives (layouts, partials, components), and change content through the content source, never by editing generated output in `dist/`, `_site/` or `public/`.

## Architecture

- Generate static HTML at build time whenever the content does not change per request. Static pages are the fastest, cheapest and safest website. Add server rendering or functions only for what truly needs them, such as form handling or personalization.
- Ship HTML and CSS first and JavaScript only where interaction requires it. In Astro, render components to HTML and hydrate only the islands that need it with `client:visible`, `client:idle` or `client:load`, in that order of preference.
- Separate content, layout and presentation: content in Markdown or the CMS with front matter validated by a schema (content collections with a schema in Astro), layouts and partials for structure, and tokens and stylesheets for appearance.
- Keep one base layout that owns the document head, the metadata, the skip link, the header, the footer and the global styles, so every page gets them right.

## Project structure

```
src/
  pages/                   # One file per route
  layouts/
    Base.astro             # Document head, metadata, header, footer
  components/              # Reusable sections and elements
  content/
    blog/                  # Markdown or MDX entries validated by a schema
  styles/
    tokens.css             # Custom properties: colors, spacing, type scale
    global.css             # Reset, base elements, layout primitives
  assets/
    images/                # Source images processed by the build
public/                    # Copied as they are: favicon, robots.txt, fonts
  fonts/                   # Self-hosted WOFF2 files
functions/                 # Serverless handlers, such as the contact form
```

- Follow the conventions of the generator in use: Hugo keeps `content/`, `layouts/`, `assets/` and `static/`, Jekyll keeps `_layouts/`, `_includes/`, `_posts/` and `_data/`, Eleventy keeps its input directory with `_includes/` and `_data/`.
- Put images that the build should optimize under the processed assets folder, and only files that must keep their exact URL, such as `favicon.ico` and `robots.txt`, in the folder copied as is.

## Patterns and practices

- Write semantic HTML: one `h1` per page, headings in order without skipped levels, `header`, `nav`, `main`, `article`, `section` with a heading, `aside` and `footer` landmarks, `ul` and `ol` for lists, `button` for actions and `a` with a real `href` for navigation.
- Declare the language with `<html lang="...">`, the encoding with `<meta charset="utf-8">` first in the head, and the viewport with `<meta name="viewport" content="width=device-width, initial-scale=1">`, never disabling zoom.
- Provide a skip link to `main`, visible on focus, and keep the focus outline visible with `:focus-visible` styles.
- Build layouts with grid and flex, size type and spacing fluidly with `clamp()`, and use container queries (`container-type: inline-size` and `@container`) for components that adapt to their slot rather than the viewport. Use media queries for page-level breakpoints, logical properties (`margin-inline`, `padding-block`) for direction-independent spacing, and avoid fixed heights on text containers.
- Define design tokens as custom properties on `:root` and reference them everywhere. Never repeat a raw color or size in a component stylesheet.
- Support dark mode by declaring `color-scheme: light dark` and redefining the color tokens under `@media (prefers-color-scheme: dark)`, or with `light-dark()`. When the site offers a manual toggle, store the choice and apply it with an inline script in the head before first paint to avoid a flash.
- Respect `prefers-reduced-motion` by disabling non-essential animation and smooth scrolling under it.
- Write JavaScript as small modules loaded with `type="module"` or `defer`, enhancing working HTML. A navigation menu, an accordion (prefer `details` and `summary`) and a dialog (prefer `dialog`) work with the keyboard and announce their state with `aria-expanded`.
- Localize with the i18n features of the generator, one URL per language (`/pt/...`), `hreflang` alternates between translations and a translated `lang` attribute.
- Comments in HTML, CSS and scripts stay rare and never ship secrets or internal notes, since they are public. JavaScript uses JSDoc (`/** ... */`) only where the project already writes it.

## SEO and metadata

- Give every page a unique, descriptive `<title>` and `<meta name="description">` written for people, a `<link rel="canonical">` with the absolute preferred URL, and Open Graph tags (`og:title`, `og:description`, `og:image` with an absolute URL, `og:url`, `og:type`) plus `twitter:card`.
- Make Open Graph images 1200 by 630 pixels, under the size the target platforms accept, with an `og:image:alt`.
- Add structured data as JSON-LD in a `<script type="application/ld+json">` using schema.org types that describe what the page really shows, such as `Organization`, `Article`, `Product`, `Event` or `BreadcrumbList`, and validate it with the Rich Results Test or the Schema Markup Validator. Never mark up content that is not visible.
- Generate `sitemap.xml` with the absolute canonical URLs of indexable pages and reference it from `robots.txt`. Use `robots.txt` to guide crawlers, never to hide private content, and mark pages that must stay out of results with `<meta name="robots" content="noindex">`.
- Keep URLs stable, lowercase and readable. When a URL changes, redirect the old one permanently with a 301 or 308 in the host configuration, and serve a real 404 page with a 404 status.
- Use descriptive link text instead of "click here", and write alternative text for meaningful images.

## Data, forms and integrations

- A static site has no server, so a form posts to a serverless function, the form service of the host or a backend the project owns. Validate every field on the server with the same rules as the client (type, length, format, allowed values) and answer with errors next to the fields.
- Protect forms from spam with a hidden honeypot field, a minimum submission time, rate limiting by address, and a challenge such as Cloudflare Turnstile, hCaptcha or reCAPTCHA when the project uses one, verified on the server with its secret.
- Use native validation attributes (`required`, `type="email"`, `minlength`, `pattern`) and `autocomplete` values for usability, never as the only validation.
- Never send form contents to an email address by embedding credentials in the page. The handler on the server holds the credentials and sends the message.
- Load content from a headless CMS at build time with the token in the build environment, and trigger a rebuild through the CMS webhook instead of fetching private content from the browser.

## Interface

- Design the smallest viewport first and confirm that nothing scrolls horizontally from 320 pixels up, that tap targets are large enough and that text stays readable at 200 percent zoom.
- Keep body text at a comfortable measure, around 60 to 75 characters per line with `max-inline-size` in `ch`, and line height around 1.5.
- Give every interactive element visible hover, focus and active states from the tokens, and never rely on hover alone to reveal content.
- Use one clear primary call to action per section of a landing page, with the same label as the destination expects.

## Privacy, consent and analytics

- Prefer privacy-friendly analytics that set no cookies and collect no personal data when the project can choose, such as Plausible, Fathom or a self-hosted tool, and document what is collected.
- Load analytics, advertising, embedded videos and other trackers that set cookies or read device identifiers only after consent where the GDPR, the ePrivacy rules, the LGPD or similar laws apply. The banner offers accept and reject with equal weight, records the choice, and lets the visitor change it later.
- When the project uses Google tags, configure Consent Mode with denied defaults before any tag loads, and update it from the banner.
- Embed third-party media in privacy-enhanced modes, such as `youtube-nocookie.com`, or behind a click-to-load facade, which also saves their weight.
- Link a privacy policy from every page footer and every form that collects personal data.

## Security

- Send a Content Security Policy from the host configuration, starting from `default-src 'self'`, listing the exact origins of scripts, styles, images, fonts, frames and form actions, with `object-src 'none'`, `base-uri 'self'` and `frame-ancestors` set. Move inline scripts and styles to files, or use hashes, so `unsafe-inline` is unnecessary.
- Send `Strict-Transport-Security` with a long `max-age` once HTTPS works on every subdomain, `X-Content-Type-Options: nosniff`, `Referrer-Policy: strict-origin-when-cross-origin` and a `Permissions-Policy` that disables the features the site does not use.
- Load third-party scripts only from origins you trust, pin versions, and add `integrity` and `crossorigin` (Subresource Integrity) to every fixed third-party file. Self-host fonts and libraries when possible.
- Keep secrets only in the build or function environment. Everything in the output folder is public, including source maps, comments and JSON data files.
- Serve the whole site over HTTPS with redirects from HTTP and from the alternate host (`www` or apex) to the canonical one.
- Add `rel="noopener noreferrer"` to links that open third-party sites in a new tab when the referrer should not leak.

## Performance

- Hold the Core Web Vitals budgets at the 75th percentile on mobile: Largest Contentful Paint within 2.5 seconds, Interaction to Next Paint within 200 milliseconds and Cumulative Layout Shift below 0.1. Set a page weight and request budget in the continuous integration when the project has Lighthouse CI.
- Serve images in AVIF or WebP with a fallback through `<picture>`, sized with `srcset` and `sizes` so each device downloads the width it needs, and always with `width` and `height` attributes (or `aspect-ratio`) so they do not shift the layout. Use the image pipeline of the generator (`astro:assets`, Hugo image processing, the Eleventy Image plugin) instead of committing hand-resized copies.
- Load images below the fold with `loading="lazy"` and `decoding="async"`. Never lazy load the largest image of the first view, and give it `fetchpriority="high"`, or preload it when it is a CSS background.
- Self-host fonts as WOFF2, subset them to the scripts used, limit the families and weights, declare `font-display: swap` (or `optional` for non-essential faces), preload only the one or two faces used above the fold with `crossorigin`, and tune the fallback with `size-adjust` to reduce the shift when the font arrives.
- Inline the critical CSS of the first view when the build supports it, load the rest without blocking, and remove unused CSS. Defer every non-critical script with `defer` or `type="module"`, and never place a synchronous script in the head.
- Cache hashed assets for a year with `Cache-Control: public, max-age=31536000, immutable`, and serve HTML with `no-cache` or a short lifetime so a deploy is visible at once. Enable Brotli or gzip compression on the host.
- Reserve space for embeds, ads and late content, and avoid inserting content above existing content after load.

## Tests and checks

- Validate the generated HTML with the Nu HTML Checker (`vnu`) or `html-validate`, and fix every error. Check that every page has one `h1`, a title, a description and a canonical URL.
- Check links with a link checker such as `lychee` or the one the project uses, internal links on every build and external ones on a schedule.
- Run automated accessibility checks with axe (through `@axe-core/playwright` or `pa11y-ci`) on every template, then test with the keyboard alone and with a screen reader on the key pages. Automated tools find only part of the problems.
- Run Lighthouse on the production build, preferably with Lighthouse CI (`lhci autorun`) and assertions on performance, accessibility, best practices and SEO, and read the field data of real users when the site has it.
- Test interactive parts and forms end to end with Playwright when the project has it, including the server validation and the spam protection.
- Check the response headers of the deployed preview with `curl -I` against the security and cache rules above.

## Tooling and quality gates

- Format with Prettier (with the Astro or other template plugins the project uses), lint CSS with Stylelint and scripts with ESLint when they are configured, and type check Astro projects with `astro check`.
- Keep the build free of warnings about missing images, broken references or invalid front matter.
- Never commit generated output unless the host requires it, and never edit it by hand.

## Build, configuration and release

- Build with the generator's command (`astro build`, `npx @11ty/eleventy`, `hugo --minify`, `bundle exec jekyll build`) and serve the output locally (`astro preview` or a static server) before reporting.
- Deploy to the static host or CDN the project uses, such as Netlify, Vercel, Cloudflare Pages, GitHub Pages or an object store behind a CDN, with preview deployments for every change when the host supports them.
- Keep redirects, headers and cache rules in the host's versioned configuration files, not in a dashboard.
- Set the site URL in the generator configuration so canonical URLs, the sitemap and Open Graph links are absolute and correct per environment, and keep previews out of search results with `noindex`.

## Pitfalls

- Hydrating a whole page or shipping a framework runtime for a static page that needs no JavaScript.
- Lazy loading the hero image, missing image dimensions, and web fonts without `font-display`.
- Duplicate or missing titles and descriptions, relative canonical or Open Graph URLs, and staging sites left indexable.
- Blocking pages in `robots.txt` that should use `noindex`, or believing `robots.txt` hides anything.
- Client-only form validation, forms with no spam protection, and secrets embedded in the page.
- Loading trackers before consent, or a banner where rejecting is harder than accepting.
- Clickable `div` and `span` elements, missing alternative text, and low contrast text over images.
- A CSP with `unsafe-inline` added to make an inline script work, and third-party scripts without integrity.
- Long cache lifetimes on HTML, so visitors keep stale pages after a deploy.

## Definition of done

- The generated HTML validates, and every page has `lang`, one `h1`, a unique title and description, a canonical URL and Open Graph tags.
- Structured data, `sitemap.xml` and `robots.txt` are correct and validated.
- The layout works from 320 pixels up, at 200 percent zoom, in light and dark schemes, and with reduced motion.
- Keyboard navigation, focus states and axe checks pass on every changed template.
- Images use modern formats, `srcset`, dimensions and correct loading priorities, and fonts are self-hosted WOFF2 with `font-display`.
- Lighthouse on the production build meets the project budgets and the Core Web Vitals targets.
- Forms validate on the server, resist spam and keep secrets on the server.
- Trackers load only after consent where required, and the privacy policy is linked.
- The host sends the Content Security Policy, HSTS and the other security headers, and cache rules match the asset kinds.
- Links are checked, redirects cover changed URLs, and the build has no warning.
- The production build was served and checked locally or on a preview deployment.
