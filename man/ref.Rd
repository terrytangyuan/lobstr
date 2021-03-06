% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/ref.R
\name{ref}
\alias{ref}
\title{Display tree of references}
\usage{
ref(..., character = FALSE)
}
\arguments{
\item{...}{One or more objects}

\item{character}{If \code{TRUE}, show references from character vector in to
global string pool}
}
\description{
This tree display focusses on the distinction between names and values.
For each reference-type object (lists, environments, and optional character
vectors), it displays the location of each component. The display
shows the connection between shared references using a locally unique id.
}
\examples{
x <- 1:100
ref(x)

y <- list(x, x, x)
ref(y)
ref(x, y)

e <- new.env()
e$e <- e
e$x <- x
e$y <- list(x, e)
ref(e)

# Can also show references to global string pool if requested
ref(c("x", "x", "y"))
ref(c("x", "x", "y"), character = TRUE)
}
\seealso{
Other object inspectors: \code{\link{ast}},
  \code{\link{sxp}}
}
\concept{object inspectors}
