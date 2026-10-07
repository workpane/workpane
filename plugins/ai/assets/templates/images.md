# Images and other media

When the task needs an image, such as an icon, an illustration, a background or a screenshot asset:

1. Use the image generation of this run when the list above says it has one, or a skill that generates images.
2. When neither exists, check whether the `codex` command line is installed. If it is, tell the reader you intend to use it, show the command, and run it only after the reader confirms. For example:

```
codex exec \
  --sandbox workspace-write \
  --skip-git-repo-check \
  'Use $imagegen to generate a beautiful medieval fantasy button background with transparent background. The final asset must be exactly 180x50 pixels and saved as ./button.png. If the image generator returns a different resolution, crop or resize it locally to exactly 180x50 using Python and Pillow, sips, or another available image tool. Preserve transparency. Do not finish until ./button.png exists and is exactly 180x50 PNG.'
```

3. When no generator is available, say so and describe the asset the reader should provide, with its size, format and where it goes.

Always produce the exact size, format and transparency the project needs, in the folder where the project keeps its assets, with every density variant the platform expects. Never leave a placeholder image in the final result.
